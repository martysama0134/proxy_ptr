#include <doctest/doctest.h>
#include <proxy_ptr/proxy_ptr.h>

#include <set>
#include <string>
#include <unordered_set>

// ── Basic lifecycle ─────────────────────────────────────────────────────────

TEST_CASE("proxy_owner_ptr creates and deletes") {
    auto owner = proxy::make_proxy<std::string>("hello");
    CHECK(owner.alive());
    CHECK(*owner.get() == "hello");

    owner.proxy_delete();
    CHECK(owner.expired());
    CHECK(owner.get() == nullptr);
}

TEST_CASE("proxy_ptr observes owner lifetime") {
    auto owner = proxy::make_proxy<std::string>("monkey");
    proxy::proxy_ptr<std::string> obs = owner;

    CHECK(obs.alive());
    CHECK(*obs.get() == "monkey");

    owner.proxy_delete();
    CHECK(obs.expired());
    CHECK(obs.get() == nullptr);
}

TEST_CASE("multiple observers share state") {
    auto owner = proxy::make_proxy<std::string>("shared");
    proxy::proxy_ptr<std::string> a = owner;
    proxy::proxy_ptr<std::string> b = a;
    proxy::proxy_ptr<std::string> c = b;

    CHECK(a.alive());
    CHECK(b.alive());
    CHECK(c.alive());
    CHECK(a.get() == b.get());
    CHECK(b.get() == c.get());

    owner.proxy_delete();
    CHECK(a.expired());
    CHECK(b.expired());
    CHECK(c.expired());
}

TEST_CASE("proxy_owner_ptr is move-only") {
    auto a = proxy::make_proxy<int>(42);
    auto raw = a.get();

    auto b = std::move(a);
    CHECK(b.alive());
    CHECK(b.get() == raw);
    CHECK_FALSE(a.alive());  // moved-from
}

TEST_CASE("proxy_ptr copy keeps both alive") {
    auto owner = proxy::make_proxy<int>(10);
    proxy::proxy_ptr<int> a = owner;
    proxy::proxy_ptr<int> b = a;

    CHECK(a.alive());
    CHECK(b.alive());
    CHECK(a.get() == b.get());
}

TEST_CASE("self-assignment does not corrupt state") {
    auto owner = proxy::make_proxy<int>(77);
    proxy::proxy_ptr<int> obs = owner;

    // self-assign observer (was UAF when refcount == 1)
    obs = obs;
    CHECK(obs.alive());
    CHECK(*obs.get() == 77);

    // self-assign via another reference
    proxy::proxy_ptr<int>& ref = obs;
    obs = ref;
    CHECK(obs.alive());
}

// ── Null / default state ────────────────────────────────────────────────────

TEST_CASE("default-constructed proxy_ptr is expired") {
    proxy::proxy_ptr<int> p;
    CHECK(p.expired());
    CHECK(p.get() == nullptr);
    CHECK(p.hashkey() == nullptr);
}

TEST_CASE("nullptr-constructed proxy_ptr is expired") {
    proxy::proxy_ptr<int> p = nullptr;
    CHECK(p.expired());
}

TEST_CASE("assign nullptr resets proxy_ptr") {
    auto owner = proxy::make_proxy<int>(5);
    proxy::proxy_ptr<int> p = owner;
    CHECK(p.alive());

    p = nullptr;
    CHECK(p.expired());
    CHECK(owner.alive());  // owner unaffected
}

// ── Comparison operators ────────────────────────────────────────────────────

TEST_CASE("proxy_ptr vs nullptr comparisons") {
    proxy::proxy_ptr<int> null;
    auto owner = proxy::make_proxy<int>(1);
    proxy::proxy_ptr<int> valid = owner;

    CHECK((null == nullptr));
    CHECK((nullptr == null));
    CHECK_FALSE((null != nullptr));

    CHECK((valid != nullptr));
    CHECK((nullptr != valid));
    CHECK_FALSE((valid == nullptr));
}

TEST_CASE("proxy_ptr vs raw pointer comparisons") {
    auto owner = proxy::make_proxy<int>(99);
    proxy::proxy_ptr<int> obs = owner;
    int* raw = owner.get();

    CHECK((obs == raw));
    CHECK((raw == obs));
    CHECK_FALSE((obs != raw));

    int other = 0;
    CHECK((obs != &other));
    CHECK((&other != obs));
}

TEST_CASE("proxy_ptr vs proxy_ptr comparisons") {
    auto a = proxy::make_proxy<int>(1);
    auto b = proxy::make_proxy<int>(2);
    proxy::proxy_ptr<int> oa = a;
    proxy::proxy_ptr<int> ob = b;
    proxy::proxy_ptr<int> oa2 = a;

    CHECK((oa == oa2));
    CHECK((oa != ob));
    CHECK((oa < ob) != (ob < oa));  // strict ordering
}

// ── Hash containers ─────────────────────────────────────────────────────────

TEST_CASE("proxy_ptr works in unordered_set") {
    std::unordered_set<proxy::proxy_ptr<std::string>> s;
    auto e1 = proxy::make_proxy<std::string>("a");
    auto e2 = proxy::make_proxy<std::string>("b");
    s.insert(e1);
    s.insert(e2);

    CHECK(s.size() == 2);
    CHECK(s.find(e1) != s.end());
    CHECK(s.find(e2) != s.end());

    // hashkey survives proxy_delete — element stays findable
    e1.proxy_delete();
    CHECK(s.find(e1) != s.end());
}

TEST_CASE("proxy_ptr works in ordered set") {
    std::set<proxy::proxy_ptr<int>> s;
    auto a = proxy::make_proxy<int>(1);
    auto b = proxy::make_proxy<int>(2);
    proxy::proxy_ptr<int> oa = a;
    proxy::proxy_ptr<int> ob = b;
    s.insert(oa);
    s.insert(ob);

    CHECK(s.size() == 2);
}

// ── Inheritance ─────────────────────────────────────────────────────────────

class Base {
   public:
    virtual ~Base() = default;
};
class Derived : public Base {};

TEST_CASE("static_pointer_cast preserves state") {
    auto d = proxy::make_proxy<Derived>();
    auto b = proxy::static_pointer_cast<Base>(d);

    CHECK(b.alive());
    CHECK(d.alive());

    d.proxy_delete();
    CHECK(b.expired());
    CHECK(d.expired());
}

TEST_CASE("static_pointer_cast works with atomic proxies") {
    auto d = proxy::make_proxy_atomic<Derived>();
    auto b = proxy::static_pointer_cast<Base>(d);

    CHECK(b.alive());
    d.proxy_delete();
    CHECK(b.expired());
}

TEST_CASE("dynamic_pointer_cast succeeds for correct type") {
    auto d = proxy::make_proxy<Derived>();
    auto b = proxy::static_pointer_cast<Base>(d);
    auto back = proxy::dynamic_pointer_cast<Derived>(b);

    CHECK(back.alive());
    CHECK(back.get() == d.get());
}

TEST_CASE("dynamic_pointer_cast returns null for wrong type") {
    class Other : public Base {};
    auto d = proxy::make_proxy<Derived>();
    auto b = proxy::static_pointer_cast<Base>(d);
    auto wrong = proxy::dynamic_pointer_cast<Other>(b);

    CHECK(wrong.expired());
}

// ── proxy_parent_base / enable_proxy_from_this ──────────────────────────────

class Entity : public proxy::enable_proxy_from_this<Entity> {
   public:
    std::string name;
    Entity(std::string n = "default") : name(std::move(n)) {}
};

class Character : public Entity {
   public:
    int level;
    Character(std::string n, int lvl) : Entity(std::move(n)), level(lvl) {}
};

TEST_CASE("proxy_from_this returns valid observer") {
    auto owner = proxy::make_proxy<Entity>("test");
    auto obs = owner->proxy_from_this();

    CHECK(obs.alive());
    CHECK(obs->name == "test");

    owner.proxy_delete();
    CHECK(obs.expired());
}

TEST_CASE("proxy_from_this on stack object") {
    proxy::proxy_ptr<Entity> obs;
    {
        Entity e("stack");
        obs = e.proxy_from_this();
        CHECK(obs.alive());
        CHECK(obs->name == "stack");
    }
    // destructor calls proxy_delete via proxy_parent_base
    CHECK(obs.expired());
}

TEST_CASE("proxy_from_base with derived type") {
    auto owner = proxy::make_proxy<Character>("hero", 10);
    auto base_obs = owner->proxy_from_this();
    auto derived_obs = owner->proxy_from_base<Character>();

    CHECK(base_obs.alive());
    CHECK(derived_obs.alive());
    CHECK(derived_obs->level == 10);

    owner.proxy_delete();
    CHECK(base_obs.expired());
    CHECK(derived_obs.expired());
}

TEST_CASE("proxy_parent_base proxy_delete invalidates all proxies") {
    Entity e("local");
    auto p1 = e.proxy();
    auto p2 = e.proxy_from_this();

    CHECK(p1.alive());
    CHECK(p2.alive());

    e.proxy_delete();
    CHECK(p1.expired());
    CHECK(p2.expired());
}

// ── Linked references ───────────────────────────────────────────────────────

class Party;
class Member {
   public:
    proxy::proxy_ptr<Party> party;
};

class Party : public proxy::enable_proxy_from_this<Party> {
   public:
    void link(proxy::proxy_ptr<Member> m) { m->party = proxy_from_this(); }
};

TEST_CASE("cross-referenced proxies track lifetime") {
    auto member = proxy::make_proxy<Member>();
    {
        auto party = proxy::make_proxy<Party>();
        party->link(member);

        CHECK(member->party.alive());
    }
    // party destroyed — member's reference is now expired
    CHECK(member->party.expired());
}

// ── Raw memory / new+delete ─────────────────────────────────────────────────

TEST_CASE("proxy_from_this on raw new/delete object") {
    proxy::proxy_ptr<Entity> obs;

    auto* obj = new Entity("raw");
    obs = obj->proxy_from_this();
    CHECK(obs.alive());
    CHECK(obs->name == "raw");

    delete obj;
    CHECK(obs.expired());
}

// ── Array support ───────────────────────────────────────────────────────────

TEST_CASE("make_proxy with array type") {
    auto arr = proxy::make_proxy<char[]>(100);
    CHECK(arr.alive());

    arr.proxy_delete();
    CHECK(arr.expired());
}

TEST_CASE("array proxy uses array delete") {
    // Allocates with new[], must delete with delete[] (not scalar delete)
    auto arr = proxy::make_proxy<int[]>(50);
    proxy::proxy_ptr<int[]> obs = arr;

    CHECK(arr.alive());
    CHECK(obs.alive());

    arr.proxy_delete();
    CHECK(obs.expired());
}

// ── Weakref debugging ───────────────────────────────────────────────────────

TEST_CASE("_is_weakref distinguishes owner from proxy_from_this") {
    auto owner = proxy::make_proxy<Entity>();
    auto weak = owner->proxy_from_this();

    CHECK_FALSE(owner._is_weakref());
    CHECK(weak._is_weakref());
}

// ── proxy_release ───────────────────────────────────────────────────────────

TEST_CASE("proxy_release detaches without deleting") {
    auto owner = proxy::make_proxy<int>(42);
    proxy::proxy_ptr<int> obs = owner;

    int* raw = owner.proxy_release();
    CHECK(raw != nullptr);
    CHECK(owner.expired());
    CHECK(obs.expired());

    delete raw;  // manual cleanup
}

// ── Atomic mode ─────────────────────────────────────────────────────────────

TEST_CASE("make_proxy_atomic basic lifecycle") {
    auto owner = proxy::make_proxy_atomic<int>(7);
    proxy::proxy_ptr<int, proxy::proxy_atomic> obs = owner;

    CHECK(obs.alive());
    CHECK(*obs.get() == 7);

    owner.proxy_delete();
    CHECK(obs.expired());
}

TEST_CASE("proxy_factory creates proxies") {
    auto p = proxy::proxy_factory<int, proxy::proxy_non_atomic>::make(99);
    CHECK(p.alive());
    CHECK(*p.get() == 99);
}

// ── Custom deleters ─────────────────────────────────────────────────────────

static bool g_custom_deleter_called = false;
struct TestDeleter {
    void operator()(int* p) {
        g_custom_deleter_called = true;
        delete p;
    }
};

TEST_CASE("custom deleter is called on proxy_delete") {
    g_custom_deleter_called = false;
    auto owner = proxy::proxy_owner_ptr<int>(new int(42), TestDeleter{});
    CHECK(owner.alive());
    CHECK_FALSE(g_custom_deleter_called);

    owner.proxy_delete();
    CHECK(g_custom_deleter_called);
    CHECK(owner.expired());
}

TEST_CASE("lambda deleter works (non-default-constructible)") {
    bool deleted = false;
    auto deleter = [&deleted](int* p) {
        deleted = true;
        delete p;
    };
    auto owner = proxy::proxy_owner_ptr<int>(new int(7), deleter);
    CHECK(owner.alive());
    CHECK_FALSE(deleted);

    owner.proxy_delete();
    CHECK(deleted);
    CHECK(owner.expired());
}

// ── Same-state assignment ───────────────────────────────────────────────────

TEST_CASE("assign between observers of same state is safe") {
    auto owner = proxy::make_proxy<int>(5);
    proxy::proxy_ptr<int> a = owner;
    proxy::proxy_ptr<int> b = owner;

    // a and b share state — assignment should be a no-op
    a = b;
    CHECK(a.alive());
    CHECK(b.alive());
    CHECK(a.get() == b.get());
}

// ── Move-only constructor args ──────────────────────────────────────────────

TEST_CASE("make_proxy supports move-only constructor args") {
    struct MoveOnly {
        std::unique_ptr<int> val;
        MoveOnly(std::unique_ptr<int> v) : val(std::move(v)) {}
    };
    auto owner = proxy::make_proxy<MoveOnly>(std::make_unique<int>(42));
    CHECK(owner.alive());
    CHECK(*owner->val == 42);
}
