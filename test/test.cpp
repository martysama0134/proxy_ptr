#include "../include/linked_ptr/linked_ptr.h"
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
    auto size = 5000;
    constexpr auto array_size = 100000;
    constexpr auto times = 10;

    auto t3 = get_time();
    for (int t = 0; t < times; t++) {
        auto s = linked::make_linked<char[]>(size);
        std::vector<linked::linked_ptr<char[]>> ar;
        ar.resize(array_size);
        for (int i = 0; i < array_size; i++)
            ar[i] = s;
        s.linked_delete();
    }
    auto t4 = get_time();

    std::cout << "linked_ptr time : " << t4 - t3 << " seconds" << std::endl;

    t3 = get_time();
    for (int t = 0; t < times; t++) {
        auto s = new char[size];
        std::vector<char*> ar;
        ar.resize(array_size);
        for (int i = 0; i < array_size; i++)
            ar[i] = s;
        for (int i = 0; i < array_size; i++)
            ar[i] = nullptr;
        delete[] s;
        s = nullptr;
    }

    t4 = get_time();

    std::cout << "raw_ptr time : " << t4 - t3 << " seconds" << std::endl;

    std::string in;
    std::cin >> in;
}
