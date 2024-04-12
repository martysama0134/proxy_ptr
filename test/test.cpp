#include "../include/proxy_ptr/proxy_ptr.h"
#include <iostream>
#include <chrono>
#include <array>
#include <set>
#include <unordered_set>
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
                if (copy.hashkey() != root.hashkey())
                    std::cout << "what the hell\n";
        return root.hashkey();
    });

    execute_print_time("proxy atomic >> huge copy", TIMES, []() {
        auto root = proxy::make_proxy_atomic<char[]>(100000);
        for (int i = 0; i < 100000; i++)
            if (auto copy = root)
                if (copy.hashkey() != root.hashkey())
                    std::cout << "what the hell\n";
        return root.hashkey();
    });
}

void PrintTest() {
    std::cout << "monkey==" << std::endl;
    auto root = proxy::make_proxy<std::string>("monkey");
    auto root2 = root;
    auto root3 = root2;
    std::cout << *root.hashkey() << std::endl;
    std::cout << *root2.hashkey() << std::endl;
    std::cout << *root3.hashkey() << std::endl;
    printf("%s\n", root3.hashkey()->c_str());
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

    std::cout << "root ptr " << root.get() << std::endl;

    // still valid till here
    root.reset();
    root2.reset();
    
    std::cout << "root " << (root ? "alive" : "expired") << std::endl;
    std::cout << "root2 " << (root2 ? "alive" : "expired") << std::endl;
    std::cout << "root3 " << (!root3.expired() ? "alive" : "expired") << std::endl;

    std::cout << "root ptr " << root.get() << std::endl;
}

void GetPtrTest() {
    auto root = proxy::make_proxy<std::string>("monkey");
    auto root2 = root;
    auto root3 = root2;
    std::cout << "root ptr " << root.get() << std::endl;

    root3.proxy_release();
    std::cout << "root ptr " << root.get() << std::endl;
}

void GetHashTest() {
    std::unordered_set<proxy::proxy_ptr<std::string>> setList;
    auto elem1 = proxy::make_proxy<std::string>("monkey1");
    auto elem2 = proxy::make_proxy<std::string>("monkey2");
    auto elem3 = proxy::make_proxy<std::string>("monkey3");
    auto elem4 = proxy::make_proxy<std::string>("monkey4");
    setList.insert(elem1);
    setList.insert(elem2);
    setList.insert(elem3);
    setList.insert(elem4);

    for (auto& elem : setList)
        std::cout << elem.hashkey() << " == " << elem.get() << std::endl;

    // unvalidate the 3rd proxy
    auto elem3b = elem3; // copy
    std::cout << "unvalidating the ptrs..." << std::endl;
    elem3.proxy_delete();

    for (auto& elem : setList)
        std::cout << elem.hashkey() << " == " << elem.get() << std::endl;

    if (elem3 == nullptr)
        std::cout << "elem3 is null and returns true if compared to nullptr" << std::endl;
    else
        std::cout << "BUG elem3 is null and returns false if compared to nullptr" << std::endl;

    if (setList.contains(elem3))
        std::cout << "elem3 is null and is found inside setList" << std::endl;
    else
        std::cout << "BUG elem3 is null and is not found inside setList" << std::endl;

    // unvalidate all the proxies
    elem1.proxy_delete();
    elem2.proxy_delete();
    elem3.proxy_delete();
    elem4.proxy_delete();

    for (auto& elem : setList)
        std::cout << elem.hashkey() << " == " << elem.get() << std::endl;

    if (auto it = setList.find(elem1); it != setList.end())
        std::cout << "elem1 is null and has been found! " << it->hashkey() << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem1 is null and has not been found!" << std::endl;

    if (auto it = setList.find(elem2); it != setList.end())
        std::cout << "elem2 is null and has been found! " << it->hashkey() << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem2 is null and has not been found!" << std::endl;

    if (auto it = setList.find(elem3); it != setList.end())
        std::cout << "elem3 is null and has been found! " << it->hashkey() << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem3 is null and has not been found!" << std::endl;

    if (auto it = setList.find(elem4); it != setList.end())
        std::cout << "elem4 is null and has been found! " << it->hashkey() << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem4 is null and has not been found!" << std::endl;
}

class BaseShared : public std::enable_shared_from_this<BaseShared> {
   public:
    auto smth() { return shared_from_this(); }
};

class DerivedShared : public BaseShared {
   public:
    auto smth() { return std::static_pointer_cast<DerivedShared>(shared_from_this()); }
};

class BaseProxy : public proxy::enable_proxy_from_this<BaseProxy> {
   public:
    auto smth() { return proxy_from_this(); }
};

class DerivedProxy : public BaseProxy {

};

void InheritTest() {
    auto derived = proxy::make_proxy<DerivedProxy>();
    auto derived2 = derived->proxy_from_this();
    //auto derived3 = derived->proxy_from_base<DerivedProxy>(); //todo kaboom missing reference sharing

    std::cout << "derived " << derived.hashkey() << " == " << derived.get() << std::endl;
    std::cout << "derived2 " << derived2.hashkey() << " == " << derived2.get() << std::endl;
    //std::cout << "derived3 " << derived3.hashkey() << " == " << derived3.get() << std::endl;
}

int main() {
    std::cout << "Starting the tests..." << std::endl;

    //BenchTest();
    //PrintTest();
    //PrintSharedTest();
    //GetPtrTest();
    //GetHashTest();
    InheritTest();

    std::cout << "All tests completed." << std::endl;
    std::getchar();
    std::getchar();
    return 0;
}
