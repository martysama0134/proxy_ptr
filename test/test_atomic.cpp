#include <doctest/doctest.h>
#include <proxy_ptr/proxy_ptr.h>

#include <atomic>
#include <thread>
#include <vector>

TEST_CASE("atomic: concurrent copy from multiple threads") {
    auto owner = proxy::make_proxy_atomic<int>(42);
    constexpr int N_THREADS = 8;
    constexpr int N_COPIES = 10000;
    std::atomic<int> alive_count{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&owner, &alive_count]() {
            for (int i = 0; i < N_COPIES; ++i) {
                proxy::proxy_ptr<int, proxy::proxy_atomic> copy = owner;
                if (copy.alive())
                    alive_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads)
        th.join();

    CHECK(alive_count.load() == N_THREADS * N_COPIES);
    CHECK(owner.alive());
}

TEST_CASE("atomic: concurrent copy while deleting") {
    auto owner = proxy::make_proxy_atomic<int>(99);
    constexpr int N_THREADS = 8;
    constexpr int N_COPIES = 10000;

    std::atomic<bool> start{false};
    std::atomic<int> observed_alive{0};
    std::atomic<int> observed_expired{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {}
            for (int i = 0; i < N_COPIES; ++i) {
                proxy::proxy_ptr<int, proxy::proxy_atomic> copy = owner;
                if (copy.alive())
                    observed_alive.fetch_add(1, std::memory_order_relaxed);
                else
                    observed_expired.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    // Delete midway through the concurrent copies
    std::this_thread::yield();
    owner.proxy_delete();

    for (auto& th : threads)
        th.join();

    // All copies must have seen either alive or expired — no crashes
    CHECK(observed_alive.load() + observed_expired.load() == N_THREADS * N_COPIES);
    CHECK(owner.expired());
}

TEST_CASE("atomic: concurrent copy and destroy proxies") {
    auto owner = proxy::make_proxy_atomic<int>(7);
    constexpr int N_THREADS = 8;
    constexpr int N_ITERS = 10000;

    std::vector<std::thread> threads;
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&owner]() {
            for (int i = 0; i < N_ITERS; ++i) {
                // create and immediately destroy a copy — exercises ref counting
                proxy::proxy_ptr<int, proxy::proxy_atomic> tmp = owner;
                (void)tmp.alive();
            }
        });
    }

    for (auto& th : threads)
        th.join();

    CHECK(owner.alive());
    owner.proxy_delete();
    CHECK(owner.expired());
}
