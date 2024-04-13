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
    root3.proxy_delete();

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
    std::cout << "root3 " << (!root3.expired() ? "alive" : "expired")
              << std::endl;

    std::cout << "root ptr " << root.get() << std::endl;

    // still valid till here
    root.reset();
    root2.reset();

    std::cout << "root " << (root ? "alive" : "expired") << std::endl;
    std::cout << "root2 " << (root2 ? "alive" : "expired") << std::endl;
    std::cout << "root3 " << (!root3.expired() ? "alive" : "expired")
              << std::endl;

    std::cout << "root ptr " << root.get() << std::endl;
}

void GetPtrTest() {
    auto root = proxy::make_proxy<std::string>("monkey");
    auto root2 = root;
    auto root3 = root2;
    std::cout << "root ptr " << root.get() << std::endl;

    root3.proxy_delete();
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
    auto elem3b = elem3;  // copy
    std::cout << "unvalidating the ptrs..." << std::endl;
    elem3.proxy_delete();

    for (auto& elem : setList)
        std::cout << elem.hashkey() << " == " << elem.get() << std::endl;

    if (elem3 == nullptr)
        std::cout << "elem3 is null and returns true if compared to nullptr"
                  << std::endl;
    else
        std::cout
            << "BUG elem3 is null and returns false if compared to nullptr"
            << std::endl;

    if (setList.contains(elem3))
        std::cout << "elem3 is null and is found inside setList" << std::endl;
    else
        std::cout << "BUG elem3 is null and is not found inside setList"
                  << std::endl;

    // unvalidate all the proxies
    elem1.proxy_delete();
    elem2.proxy_delete();
    elem3.proxy_delete();
    elem4.proxy_delete();

    for (auto& elem : setList)
        std::cout << elem.hashkey() << " == " << elem.get() << std::endl;

    if (auto it = setList.find(elem1); it != setList.end())
        std::cout << "elem1 is null and has been found! " << it->hashkey()
                  << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem1 is null and has not been found!" << std::endl;

    if (auto it = setList.find(elem2); it != setList.end())
        std::cout << "elem2 is null and has been found! " << it->hashkey()
                  << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem2 is null and has not been found!" << std::endl;

    if (auto it = setList.find(elem3); it != setList.end())
        std::cout << "elem3 is null and has been found! " << it->hashkey()
                  << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem3 is null and has not been found!" << std::endl;

    if (auto it = setList.find(elem4); it != setList.end())
        std::cout << "elem4 is null and has been found! " << it->hashkey()
                  << " == " << it->get() << std::endl;
    else
        std::cout << "BUG elem4 is null and has not been found!" << std::endl;
}

class BaseProxyTest {
   public:
    virtual ~BaseProxyTest() { std::cout << "~BaseProxyTest" << std::endl; }
};
class DerivedProxyTest : public BaseProxyTest {
   public:
    ~DerivedProxyTest() { std::cout << "~DerivedProxyTest" << std::endl; }
};

void InheritTest() {
    auto derived = proxy::make_proxy<DerivedProxyTest>();
    auto derived2 =
        proxy::static_pointer_cast<BaseProxyTest>(derived);  // todo kaboom
    derived2.proxy_delete();
    // it must call both destructor
    // checking derived is no longer alive
    if (!derived)
        std::cout << "derived is no longer alive." << std::endl;
    if (!derived2)
        std::cout << "derived2 is no longer alive." << std::endl;
}

void ParentBaseDeleteTest() {
    struct ParentBaseTest : proxy::proxy_parent_base<ParentBaseTest> {};
    struct DerivedTest : ParentBaseTest {};

    // constructing a proxy parent base object
    {
        DerivedTest object;

        // generating proxy pointers
        auto pr1 = object.proxy();
        auto pr2 = object.proxy();
        auto pr3 = object.proxy_from_base<DerivedTest>();

        // calling proxy_delete on one of the proxy generated
        pr1.proxy_delete();

        // checking value of proxy pointers
        std::cout << "pr1.alive = " << pr1.alive() << std::endl;
        std::cout << "pr1.ptr = " << pr1.get() << std::endl;
        std::cout << "pr2.alive = " << pr2.alive() << std::endl;
        std::cout << "pr2.ptr = " << pr2.get() << std::endl;
        std::cout << "pr3.alive = " << pr3.alive() << std::endl;
        std::cout << "pr3.ptr = " << pr3.get() << std::endl;
    }

    // checking for proxy_from_this
    {
        std::cout << "\n\nsecond test:" << std::endl;
        auto derived = proxy::make_proxy<DerivedTest>();
        auto base = derived->proxy_from_this();
        base.proxy_delete();

        std::cout << "derived.alive " << derived.alive() << std::endl;
        std::cout << "derived.ptr " << derived.get() << std::endl;
        std::cout << "base.alive " << base.alive() << std::endl;
        std::cout << "base.ptr " << base.get() << std::endl;
    }
}

class ValidBaseTest : public proxy::enable_proxy_from_this<ValidBaseTest> {
   public:
    std::string name;
    int id;
    ValidBaseTest() : name("NONAME"), id(123) {}
    ValidBaseTest(std::string _name, int _id) : name(_name), id(_id) {}
};

class ValidDerivedTest : public ValidBaseTest {
   public:
    std::string subname;
    int subid;
    ValidDerivedTest(std::string _name, int _id, std::string _subname,
                     int _subid)
        : ValidBaseTest(_name, _id), subname(_subname), subid(_subid) {}
};

void ValidInheritTest() {
    auto derived =
        proxy::make_proxy<ValidDerivedTest>("mname", 111, "msubname", 222);
    auto base = derived->proxy_from_this();
    auto rederived = derived->proxy_from_base<ValidDerivedTest>();

    std::cout << "\nexpecting they are all alive and valid:" << std::endl;
    std::cout << "derived ptr " << derived.get() << " name " << derived->name
              << " id " << derived->id << " subname " << derived->subname
              << " subid " << derived->subid << std::endl;
    std::cout << "base ptr " << derived.get() << " name " << derived->name
              << " id " << derived->id << std::endl;
    std::cout << "rederived ptr " << rederived.get() << " name "
              << rederived->name << " id " << rederived->id << " subname "
              << rederived->subname << " subid " << rederived->subid
              << std::endl;

    // destroying first node of proxy
    rederived.proxy_delete();
    std::cout << "\nexpecting derived has a value while base and rederived are "
                 "no longer alive :"
              << std::endl;
    std::cout << "derived ptr " << derived.get() << " alive " << derived.alive()
              << std::endl;
    std::cout << "base ptr " << base.get() << " alive " << base.alive()
              << std::endl;
    std::cout << "rederived ptr " << rederived.get() << " alive "
              << rederived.alive() << std::endl;

    // destroying the second node of proxy
    derived.proxy_delete();
    std::cout << "\nexpecting all of them are no longer alive:" << std::endl;
    std::cout << "derived ptr " << derived.get() << " alive " << derived.alive()
              << std::endl;
    std::cout << "base ptr " << base.get() << " alive " << base.alive()
              << std::endl;
    std::cout << "rederived ptr " << rederived.get() << " alive "
              << rederived.alive() << std::endl;
}

class EntityTest : public proxy::enable_proxy_from_this<EntityTest> {
   public:
    std::string name;
    int id;
    EntityTest() : name("NONAME"), id(123) {}
    EntityTest(std::string _name, int _id) : name(_name), id(_id) {}
};

class CharacterTest : public EntityTest {
   public:
    std::string subname;
    int subid;
    CharacterTest(std::string _name, int _id, std::string _subname, int _subid)
        : EntityTest(_name, _id), subname(_subname), subid(_subid) {}
};

void FullNodeInheritTest() {
    {
        auto character =
            proxy::make_proxy<CharacterTest>("mname", 111, "msubname", 222);
        auto entity = proxy::static_pointer_cast<EntityTest>(character);

        entity.proxy_delete();

        std::cout << "character ptr " << character.get() << " hashkey "
                  << character.hashkey() << " alive " << character.alive()
                  << std::endl;
        std::cout << "entity ptr " << entity.get() << " hashkey "
                  << entity.hashkey() << " alive " << entity.alive()
                  << std::endl;
    }

    {
        auto character =
            proxy::make_proxy<CharacterTest>("mname", 111, "msubname", 222);
        auto entity = proxy::static_pointer_cast<EntityTest>(character);

        auto base = entity->proxy_from_this();
        base.proxy_delete();

        std::cout << "character ptr " << character.get() << " hashkey "
                  << character.hashkey() << " alive " << character.alive()
                  << std::endl;
        std::cout << "entity ptr " << entity.get() << " hashkey "
                  << entity.hashkey() << " alive " << entity.alive()
                  << std::endl;
        std::cout << "base ptr " << base.get() << " hashkey " << base.hashkey()
                  << " alive " << base.alive() << std::endl;
    }
}

void DebuggingWeakrefTest() {
    auto ptr = proxy::make_proxy<EntityTest>();
    auto weakptr = ptr->proxy_from_this();

    std::cout << "expecting 0-1" << std::endl;
    std::cout << "result: " << ptr._is_weakref() << "-" << weakptr._is_weakref()
              << std::endl;
}

int main() {
    std::cout << "Starting the tests..." << std::endl;

    // BenchTest();
    // PrintTest();
    // PrintSharedTest();
    // GetPtrTest();
    // GetHashTest();
    // InheritTest();
    // ParentBaseDeleteTest();
    // ValidInheritTest();
    // FullNodeInheritTest();
    DebuggingWeakrefTest();

    std::cout << "All tests completed." << std::endl;
    std::getchar();
    std::getchar();
    return 0;
}
