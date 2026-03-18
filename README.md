# proxy_ptr

A lightweight, header-only C++17 library providing non-owning observer pointers (`proxy_ptr`) and owning pointers (`proxy_owner_ptr`). Approximately **20x faster** than `std::shared_ptr` for copy-heavy workloads.

## Features

- **`proxy_owner_ptr<T>`** — move-only owning pointer, the only type that can call `proxy_delete()`
- **`proxy_ptr<T>`** — copyable observer that tracks whether the pointed-to object is still alive
- **`enable_proxy_from_this<T>`** — CRTP base class for objects that generate proxy observers
- **Atomic mode** — `make_proxy_atomic<T>()` for thread-safe reference counting
- **Pointer casts** — `static_pointer_cast`, `dynamic_pointer_cast`, `const_pointer_cast`, `reinterpret_pointer_cast`
- **Container support** — `std::hash` and `std::less` specializations for use in `std::unordered_set`, `std::set`, etc.

## Quick start

```cpp
#include <proxy_ptr/proxy_ptr.h>

// Create an owning proxy
auto owner = proxy::make_proxy<std::string>("hello");

// Observers can be freely copied — they cannot delete
proxy::proxy_ptr<std::string> obs = owner;
assert(obs.alive());
assert(*obs.get() == "hello");

// Only the owner can delete
owner.proxy_delete();
assert(obs.expired());     // all observers see the deletion
assert(obs.get() == nullptr);
```

### enable_proxy_from_this

```cpp
class Entity : public proxy::enable_proxy_from_this<Entity> {
public:
    std::string name;
    Entity(std::string n) : name(std::move(n)) {}
};

// Works with stack, heap, or make_proxy objects
Entity e("stack_obj");
auto obs = e.proxy_from_this();
assert(obs.alive());
// obs becomes expired when e is destroyed
```

### Linked references

```cpp
class Party : public proxy::enable_proxy_from_this<Party> {
public:
    void join(proxy::proxy_ptr<Member> m) {
        m->party = proxy_from_this();
    }
};

auto member = proxy::make_proxy<Member>();
{
    auto party = proxy::make_proxy<Party>();
    party->join(member);
    assert(member->party.alive());
}
// party destroyed — member's reference auto-expires
assert(member->party.expired());
```

### Atomic mode (thread-safe ref counting)

```cpp
auto owner = proxy::make_proxy_atomic<int>(42);
// Safe to copy/destroy proxy_ptr<int, proxy::proxy_atomic> across threads
```

## Building

Header-only — just add `include/` to your include path. Or with CMake:

```cmake
add_subdirectory(proxy_ptr)
target_link_libraries(your_target PRIVATE proxy_ptr)
```

### Running tests

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Warning

`proxy_ptr` and `proxy_owner_ptr` with the default `proxy_non_atomic` flag are **not thread-safe**. Use `make_proxy_atomic<T>()` if you need to copy/destroy observers across threads (note: `proxy_delete()` itself is not synchronized).

## License

MIT — see [LICENSE](LICENSE).
