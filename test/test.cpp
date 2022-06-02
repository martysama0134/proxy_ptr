#include "../include/linked_ptr/linked_ptr.h"
#include "../include/proxy_ptr/proxy_ptr.h"
#include <iostream>
#include <memory>
#include <chrono>
#include <array>
#include <thread>

double get_time() {
    return std::chrono::duration<double>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

int main() {
    {
        auto start = get_time();
        auto root = linked::make_linked<char[]>(1000);
        for (int j = 0; j < 1; j++) {
            for (int i = 0; i < 100000000; i++) {
                linked::linked_ptr<char[]> ptr = root;
            }
        }

        auto end = get_time();
        auto time = end - start;

        std::cout << "t1 : finish in " << time << std::endl;
    }

    {
        auto start = get_time();
        auto root = std::make_shared<char[]>(1000);
        for (int j = 0; j < 1; j++) {
            for (int i = 0; i < 100000000; i++) {
                std::shared_ptr<char[]> ptr = root;
            }
        }

        auto end = get_time();
        auto time = end - start;
        std::cout << "t2 : finish in " << time << std::endl;
    }

    {
        auto start = get_time();
        auto root = proxy::make_proxy<char[]>(1000);
        for (int j = 0; j < 1; j++) {
            for (int i = 0; i < 100000000; i++) {
                proxy::proxy_ptr<char[]> ptr1 = root;
            }
        }

        auto end = get_time();
        auto time = end - start;
        std::cout << "t3 : finish in " << time << std::endl;
    }

    std::string s;
    std::cin >> s;
    return 0;
}
