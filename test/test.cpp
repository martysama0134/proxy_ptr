#include "../include/proxy_ptr/proxy_ptr.h"
#include <iostream>
#include <chrono>
#include <array>
#include <thread>

double get_time() {
    return std::chrono::duration<double>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

template <class Func>
void execute_print_time(const std::string& name, int times, Func f) {
    auto start = get_time();
    // executing
    for (int i = 0; i < times; i++)
        f();

    // getting execution time
    auto end = get_time();
    auto time = end - start;
    std::cout << name << ": finish in " << time << std::endl;
}

void BenchTest() {
    #ifdef _DEBUG
    constexpr auto TIMES = 2000;
    #else
    constexpr auto TIMES = 20000;
    #endif

    execute_print_time("shared >> huge copy", TIMES, []() {
        auto root = std::make_shared<char[]>(100000);
        for (int i = 0; i < 100000; i++)
            if (auto copy = root)
                if (copy.get() != root.get())
                    std::cout << "what the hell\n";
        return root.get();
    });

    execute_print_time("proxy >> huge copy", TIMES, []() {
        auto root = proxy::make_proxy<char[]>(100000);
        for (int i = 0; i < 100000; i++)
            if (auto copy = root)
                if (copy.get() != root.get())
                    std::cout << "what the hell\n";
        return root.get();
    });

    execute_print_time("proxy atomic >> huge copy", TIMES, []() {
        auto root = proxy::make_proxy_atomic<char[]>(100000);
        for (int i = 0; i < 100000; i++)
            if (auto copy = root)
                if (copy.get() != root.get())
                    std::cout << "what the hell\n";
        return root.get();
    });
}

void PrintTest() {
    std::cout << "monkey==" << std::endl;
    auto root = proxy::make_proxy<std::string>("monkey");
    auto root2 = root;
    auto root3 = root2;
    std::cout << *root.get() << std::endl;
    std::cout << *root2.get() << std::endl;
    std::cout << *root3.get() << std::endl;
    printf("%s\n", root3.get()->c_str());
    std::cout << "root " << (root.alive() ? "alive" : "expired") << std::endl;
    std::cout << "root2 " << (root2.alive() ? "alive" : "expired") << std::endl;
    std::cout << "root3 " << (root3.alive() ? "alive" : "expired") << std::endl;

    // still valid till here
    root3.proxy_release();

    std::cout << "root " << (root.alive() ? "alive" : "expired") << std::endl;
    std::cout << "root2 " << (root2.alive() ? "alive" : "expired") << std::endl;
    std::cout << "root3 " << (root3.alive() ? "alive" : "expired") << std::endl;
}

void PrintSharedTest() {
    std::cout << "monkey==" << std::endl;
    auto root = std::make_shared<std::string>("monkey");
    auto root2 = root;
    std::weak_ptr<std::string> root3 = root2;

    std::cout << *root.get() << std::endl;
    std::cout << *root2.get() << std::endl;
    std::cout << *root3.lock() << std::endl;
    printf("%s\n", root.get()->c_str());

    std::cout << "root " << (root ? "alive" : "expired") << std::endl;
    std::cout << "root2 " << (root2 ? "alive" : "expired") << std::endl;
    std::cout << "root3 " << (!root3.expired() ? "alive" : "expired") << std::endl;

    // still valid till here
    root.reset();
    root2.reset();
    
    std::cout << "root " << (root ? "alive" : "expired") << std::endl;
    std::cout << "root2 " << (root2 ? "alive" : "expired") << std::endl;
    std::cout << "root3 " << (!root3.expired() ? "alive" : "expired") << std::endl;
}

int main() {
    std::cout << "Starting the tests..." << std::endl;

    //BenchTest();
    //PrintTest();
    //PrintSharedTest();

    std::cout << "All tests completed." << std::endl;
    std::getchar();
    std::getchar();
    return 0;
}
