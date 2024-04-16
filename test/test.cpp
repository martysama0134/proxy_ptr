#define TEST_OWNER_THREAD_SAFE
#include <iostream>
#include <functional>
#ifdef TEST_OWNER_THREAD_SAFE
    #include "../include/owner/owner_ts.h"
    #include <thread>
    #include <random>
#else
    #include "../include/owner/owner.h"
#endif

int g_passedTestCount = 0;
int g_failedTestCount = 0;

#define TEST(function) ExecuteTest(#function, function)
#define TEST_ASSERT(condition)                                       \
    if (!(condition)) {                                              \
        std::cout << "ASSERTION FAILED ON LINE " << __LINE__ << ": " \
                  << #condition << std::endl;                        \
        return false;                                                \
    }

void ExecuteTest(const std::string& name, std::function<bool()> func) {
    if (!func()) {
        std::cout << "FAILED TEST : " << name << std::endl;
        g_failedTestCount++;
        return;
    }
    g_passedTestCount++;
}

bool TestOwnerPtr() {
    // making an owner_ptr
    auto o1 = owner::make_owner<std::string>("prettystring");
    TEST_ASSERT(o1.get() != nullptr);
    TEST_ASSERT(o1.alive());
    TEST_ASSERT(*o1 == "prettystring");

    // making a copy
    auto copy_o1 = o1;
    TEST_ASSERT(copy_o1.get() != nullptr);
    TEST_ASSERT(copy_o1.alive());
    TEST_ASSERT(*copy_o1 == "prettystring");

    // destroying internal object
    copy_o1.owner_delete();
    TEST_ASSERT(o1.get() == nullptr);
    TEST_ASSERT(!o1.alive());

    return true;
}

bool TestWeakPtr() {
    owner::weak_ptr<std::string> weak;
    // making owner in another scope
    {
        // making an owner_ptr
        auto o1 = owner::make_owner<std::string>("prettystring");

        // making the weak ptr
        weak = owner::make_weak(o1);
        TEST_ASSERT(weak.get() != nullptr);
        TEST_ASSERT(weak.alive());
        TEST_ASSERT(*weak == "prettystring");

        // making a copy of the weak ptr
        auto weak2 = weak;
        TEST_ASSERT(weak2.get() != nullptr);
        TEST_ASSERT(weak2.alive());
        TEST_ASSERT(*weak2 == "prettystring");

        // taking back ownership from weak ptr
        auto o2 = owner::get_ownership(weak2);
        TEST_ASSERT(o2.get() != nullptr);
        TEST_ASSERT(o2.alive());
        TEST_ASSERT(*o2 == "prettystring");
    }

    // testing weak no longer alive
    TEST_ASSERT(weak.get() == nullptr);
    TEST_ASSERT(!weak.alive());

    return true;
}

bool TestProxyPtr() {
    struct ProxableString
        : public owner::enable_proxy_from_this<ProxableString> {
        std::string str;
    };

    ProxableString string;
    string.str = "prettystring";

    // making a proxy
    auto ps = string.proxy();
    TEST_ASSERT(ps.get() != nullptr);
    TEST_ASSERT(ps.alive());
    TEST_ASSERT(ps->str == "prettystring");

    // making a copy of the proxy
    auto copy_ps = ps;
    TEST_ASSERT(copy_ps.get() != nullptr);
    TEST_ASSERT(copy_ps.alive());
    TEST_ASSERT(copy_ps->str == "prettystring");

    // destroying all proxy
    string.proxy_delete();

    TEST_ASSERT(ps.get() == nullptr);
    TEST_ASSERT(!ps.alive());

    TEST_ASSERT(copy_ps.get() == nullptr);
    TEST_ASSERT(!copy_ps.alive());

    // testing for auto-deleting on destructor
    owner::proxy_ptr<ProxableString> psa;

    {
        // adding a new scope in order to call
        // destructor
        ProxableString string2;
        string2.str = "prettystring";
        psa = string2.proxy();

        TEST_ASSERT(psa.get() != nullptr);
        TEST_ASSERT(psa.alive());
        TEST_ASSERT(psa->str == "prettystring");
    }

    TEST_ASSERT(psa.get() == nullptr);
    TEST_ASSERT(!psa.alive());

    return true;
}

#ifdef TEST_OWNER_THREAD_SAFE
int RandomInt(int min, int max) {
    static std::default_random_engine eng(std::random_device{}());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(eng);
}

bool TestOwnerThreadSafe() {
    // event that makes 10 threads waiting one of them to destroy the object
    auto func = []() {
        auto owner = owner::make_owner<std::string>("prettystring");
        auto weak = owner::make_weak(owner);
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; i++) {
            auto thread_event = [&weak, &owner, index = i]() {
                // trying to delete via owner
                while (true) {
                    auto lock = owner.try_lock();
                    if (!lock)
                        return;

                    // trying to delete via owner
                    if (RandomInt(0, 10000) == 5) {
                        std::cout << "deleting via owner: thread id " << index
                                  << std::endl;
                        owner.owner_delete();
                    }

                    // trying to delete via weak
                    else if (RandomInt(0, 10000) == 6) {
                        std::cout << "deleting via weak: thread id " << index
                                  << std::endl;
                        if (auto temp_owner = owner::get_ownership(weak))
                            temp_owner.owner_delete();
                    }
                }
            };

            threads.emplace_back(std::thread(thread_event));
        }

        for (auto& thread : threads)
            thread.join();
    };

    for (int i = 0; i < 100; i++)
        func();

    return true;
}
#endif

void PrintResults() {
    if (g_failedTestCount == 0)
        std::cout << "All tests are passed." << std::endl;
    else {
        std::cout << "Passed Tests: " << g_passedTestCount << std::endl;
        std::cout << "Failed tests: " << g_failedTestCount << std::endl;
    }
}

int main() {
    std::cout << "Starting the tests..." << std::endl;

    // executing all tests
    TEST(TestOwnerPtr);
    TEST(TestWeakPtr);
    TEST(TestProxyPtr);

#ifdef TEST_OWNER_THREAD_SAFE
    TEST(TestOwnerThreadSafe);
#endif

    // printing test results
    std::cout << "All tests executed." << std::endl;
    PrintResults();

    // pause
    std::getchar();
    std::getchar();
    return 0;
}
