#include <proxy_ptr/proxy_ptr.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

template <class Func>
double measure(const std::string& name, int iterations, Func f) {
    // warmup
    for (int i = 0; i < 100; ++i)
        f();

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i)
        f();
    auto end = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << name << ": " << ms << " ms (" << iterations << " iters)\n";
    return ms;
}

int main() {
    constexpr int ITERS = 5000;
    constexpr int COPIES = 100000;

    std::cout << "=== Copy-heavy benchmark (" << COPIES << " copies x " << ITERS
              << " iters) ===\n\n";

    // ── shared_ptr ──────────────────────────────────────────────────────
    auto t_shared = measure("shared_ptr    copy", ITERS, [&]() {
        auto root = std::make_shared<int>(42);
        for (int i = 0; i < COPIES; ++i) {
            auto copy = root;
            if (!copy)
                std::abort();
        }
    });

    // ── proxy_ptr (non-atomic) ──────────────────────────────────────────
    auto t_proxy = measure("proxy_ptr     copy", ITERS, [&]() {
        auto root = proxy::make_proxy<int>(42);
        for (int i = 0; i < COPIES; ++i) {
            proxy::proxy_ptr<int> copy = root;
            if (!copy)
                std::abort();
        }
    });

    // ── proxy_ptr (atomic) ──────────────────────────────────────────────
    auto t_atomic = measure("proxy_atomic  copy", ITERS, [&]() {
        auto root = proxy::make_proxy_atomic<int>(42);
        for (int i = 0; i < COPIES; ++i) {
            proxy::proxy_ptr<int, proxy::proxy_atomic> copy = root;
            if (!copy)
                std::abort();
        }
    });

    std::cout << "\n=== Ratios ===\n";
    std::cout << "shared_ptr / proxy_ptr:    " << t_shared / t_proxy << "x\n";
    std::cout << "shared_ptr / proxy_atomic: " << t_shared / t_atomic << "x\n";
    std::cout << "proxy_atomic / proxy_ptr:  " << t_atomic / t_proxy << "x\n";

    std::cout << "\n=== Alive-check benchmark (" << COPIES << " checks x "
              << ITERS << " iters) ===\n\n";

    // ── shared_ptr alive check ──────────────────────────────────────────
    auto root_shared = std::make_shared<int>(42);
    auto t_shared_alive = measure("shared_ptr    alive", ITERS, [&]() {
        for (int i = 0; i < COPIES; ++i) {
            if (!root_shared)
                std::abort();
        }
    });

    // ── proxy_ptr alive check ───────────────────────────────────────────
    auto root_proxy = proxy::make_proxy<int>(42);
    proxy::proxy_ptr<int> obs_proxy = root_proxy;
    auto t_proxy_alive = measure("proxy_ptr     alive", ITERS, [&]() {
        for (int i = 0; i < COPIES; ++i) {
            if (!obs_proxy)
                std::abort();
        }
    });

    std::cout << "\n=== Create+Delete benchmark (" << COPIES << " objects x "
              << ITERS / 10 << " iters) ===\n\n";

    auto t_shared_create = measure("shared_ptr    create+del", ITERS / 10, [&]() {
        for (int i = 0; i < COPIES; ++i) {
            auto p = std::make_shared<int>(i);
        }
    });

    auto t_proxy_create = measure("proxy_ptr     create+del", ITERS / 10, [&]() {
        for (int i = 0; i < COPIES; ++i) {
            auto p = proxy::make_proxy<int>(i);
        }
    });

    std::cout << "\nshared_ptr / proxy_ptr create: " << t_shared_create / t_proxy_create << "x\n";

    return 0;
}
