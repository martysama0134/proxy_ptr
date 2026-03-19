# proxy_ptr Migration Guide

## What changed

`proxy_ptr<T>` is now a pure observer. `proxy_delete()` has been removed from it.
A new `proxy_owner_ptr<T>` (move-only, inherits from `proxy_ptr<T>`) is the only
type that can call `proxy_delete()`.

`make_proxy<T>()`, `make_proxy_atomic<T>()`, and `proxy_factory<T>::make()` now
return `proxy_owner_ptr<T>` instead of `proxy_ptr<T>`.

`proxy_owner_ptr<T>` implicitly converts to `proxy_ptr<T>` everywhere an observer
is expected (function parameters, assignments, containers) — no cast needed.

---

## Migration rules

### 1. Copying a make_proxy result

```cpp
// BEFORE
auto owner = proxy::make_proxy<Foo>();
auto copy  = owner;          // worked, both could proxy_delete
copy.proxy_delete();         // deleted the object

// AFTER
auto owner = proxy::make_proxy<Foo>(); // proxy_owner_ptr<Foo>, can proxy_delete
proxy_ptr<Foo> obs = owner;            // explicit observer copy — cannot proxy_delete
owner.proxy_delete();                  // only the owner can delete
```

### 2. Storing in containers

```cpp
// BEFORE
std::map<int, proxy_ptr<Foo>> mgr;
mgr[id] = proxy::make_proxy<Foo>();
mgr[id].proxy_delete();    // worked

// AFTER — manager owns the objects
std::map<int, proxy_owner_ptr<Foo>> mgr;
mgr[id] = proxy::make_proxy<Foo>();
mgr[id].proxy_delete();    // still works — it->second is the owner

// Erase-then-delete pattern (avoid dangling iterator):
auto owner = std::move(mgr[id]);
mgr.erase(id);
owner.proxy_delete();
```

### 3. Passing to functions / storing secondary refs

```cpp
// No change needed — implicit conversion from owner to observer is automatic
void process(proxy_ptr<Foo> p) { /* use p, cannot delete */ }

auto owner = proxy::make_proxy<Foo>();
process(owner);                    // passes as proxy_ptr<Foo> observer ✅
proxy_ptr<Foo> ref = owner;        // observer copy ✅
```

### 4. Deleting via a cast result

```cpp
// BEFORE
auto base = proxy::static_pointer_cast<Base>(derived_owner);
base.proxy_delete();   // worked

// AFTER — casts always return proxy_ptr<T> (observer), cannot delete
// Call proxy_delete on the original owner instead:
derived_owner.proxy_delete();
```

### 5. Deleting via proxy_from_this() / proxy_from_base()

```cpp
// BEFORE — worked but misleading (non_deleter, no memory freed)
auto p = obj.proxy_from_this();
p.proxy_delete();   // only set _alive=false, did NOT free memory

// AFTER — call proxy_delete on the object itself (proxy_parent_base method)
obj.proxy_delete();             // if obj is a stack/member variable
owner_ptr->proxy_delete();      // if obj was created via make_proxy
// OR call proxy_delete on the owner that holds the heap allocation:
owner_ptr.proxy_delete();       // frees memory + invalidates all proxies
```

### 6. LP* typedefs pattern (game server / manager pattern)

```cpp
// BEFORE
using LPFOO = proxy::proxy_ptr<Foo>;
LPFOO obj = proxy::make_proxy<Foo>(...);
obj.proxy_delete();

// AFTER
// The manager that OWNS objects:
using LPFOO       = proxy::proxy_ptr<Foo>;        // observers, unchanged
using LPFOO_OWNER = proxy::proxy_owner_ptr<Foo>;  // only in manager internals

LPFOO_OWNER obj = proxy::make_proxy<Foo>(...);    // manager stores owner
LPFOO ref = obj;                                   // other code gets observer
obj.proxy_delete();                                // manager deletes
```

---

## Quick reference

| Old code                            | New code                                   | Notes                        |
|-------------------------------------|--------------------------------------------|------------------------------|
| `auto p = make_proxy<T>()`          | unchanged — `p` is now `proxy_owner_ptr<T>`| can still call proxy_delete  |
| `proxy_ptr<T> p = make_proxy<T>()`  | unchanged — implicit observer conversion   | loses proxy_delete ability   |
| `auto copy = owner`                 | `proxy_ptr<T> copy = owner`               | owner is move-only           |
| `copy.proxy_delete()`               | `owner.proxy_delete()`                     | only owner can delete        |
| `cast_result.proxy_delete()`        | `owner.proxy_delete()`                     | casts return observers       |
| `proxy_from_this().proxy_delete()`  | `obj.proxy_delete()` or `owner.proxy_delete()` | was broken before anyway |
| container `it->second.proxy_delete()`| store `proxy_owner_ptr<T>` in container   | same call, different type    |
| `observer.proxy_release()`          | `owner.proxy_release()`                    | only owner can release       |
