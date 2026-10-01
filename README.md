# proxy_ptr

A lightweight, header-only C++17 library for **single-owner, many-observer** pointer semantics.

## The problem

When a manager (scene graph, ECS, object pool, etc.) owns objects and other systems need references to them, the standard options fall short:

- **`std::shared_ptr`** — gives every holder the power to keep the object alive. You lose centralized lifetime control.
- **`std::weak_ptr`** — requires `shared_ptr` on the owning side, which you don't want.
- **Raw pointers** — no way to know if the object was deleted. Dangling pointer bugs.

`proxy_ptr` fills the gap: **only the owner can delete the object, and every observer sees it expire at once.** Observers can check `alive()` but cannot trigger deletion. It's a one-way invalidation broadcast.

Deletion is explicit: call `proxy_delete()` on the owner. If the owner is destroyed without it, the object is kept until the last observer is released (observers share the control block).

Performance vs `std::shared_ptr` (MSVC x64 `/O2`, `test/bench.cpp`):

| Workload | proxy_ptr (non-atomic) | proxy_ptr (atomic) |
|----------|------------------------|--------------------|
| Copy-heavy | ~4.5x faster | ~same |
| `alive()` check | ~1.5x slower | — |
| Create + delete | ~1.9x slower (two allocations) | — |

## Why not `shared_ptr` / `weak_ptr`?

`shared_ptr` answers "who keeps this alive?". A manager needs "who decides when it dies, and how does everyone else find out?". With `shared_ptr`, every holder co-owns the object, so a forgotten reference keeps a logically destroyed object alive.

The fair comparison is a `shared_ptr` held only by the manager plus `weak_ptr` everywhere else:

| | `weak_ptr` | `proxy_ptr` observer |
|---|---|---|
| Use the object | `if (auto p = w.lock()) p->f();` at every use site | `p->f()` — same syntax as a raw pointer, so retrofitting existing code is mostly a typedef change |
| Owner deletes while someone is using it | deletion is deferred until the `lock()` temporary dies | deletion happens now; observers see `nullptr` |
| Cost of using it | atomic increment/decrement per `lock()` | no reference-count traffic to dereference (non-atomic mode) |
| Hashing / `==` / keyed containers | not provided (only `owner_before`) | by address, and still findable after the object dies |
| `this` → handle | `shared_from_this()`: only for `shared_ptr`-owned objects, throws in constructors | `enable_proxy_from_this`: works for stack, member, `new`-ed and `make_proxy` objects |

What `proxy_ptr` does **not** give you:
- **It doesn't prevent use-after-delete; it makes it deterministic.** Using a dead observer dereferences `nullptr` (and asserts in debug builds) instead of silently corrupting memory. Check `alive()` or re-resolve where an object may have died.
- **No cross-thread use.** Atomic mode only makes the reference count atomic.
- **Comparing addresses can still match a new object** allocated at the same address (ABA). Treat `expired()` as "changed".

For new code, `weak_ptr` or ID handles are equally valid choices. `proxy_ptr` fits single-threaded, manager-owned objects, especially existing raw-pointer code that needs safe observers without rewriting every use site.

## Features

- **`proxy_owner_ptr<T>`** — move-only owning pointer, the only type that can call `proxy_delete()`. Converts implicitly to a `proxy_ptr<T>` observer copy; it is not a subclass, so it never binds to `proxy_ptr<T>&`
- **`proxy_ptr<T>`** — copyable observer that tracks whether the pointed-to object is still alive
- **Custom deleters** — function objects, lambdas, function pointers
- **Implicit up-casts** — `proxy_ptr<Derived>` and `proxy_owner_ptr<Derived>` convert to `proxy_ptr<Base>` (address-adjusted for multiple inheritance); down-casts stay explicit via `static_pointer_cast` / `dynamic_pointer_cast`
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
