# proxy_ptr Migration Guide

## What changed

`proxy_ptr<T>` is now a pure observer. `proxy_delete()` has been removed from it.
A new `proxy_owner_ptr<T>` (move-only, wraps a `proxy_ptr<T>`) is the only
type that can call `proxy_delete()`.

`make_proxy<T>()`, `make_proxy_atomic<T>()`, and `proxy_factory<T>::make()` now
return `proxy_owner_ptr<T>` instead of `proxy_ptr<T>`.

`proxy_owner_ptr<T>` implicitly converts to `proxy_ptr<T>` everywhere an observer
is expected (by-value / `const&` parameters, assignments, containers) — no cast needed.

Two things intentionally do **not** work with an owner:

- Binding to a non-const `proxy_ptr<T>&` (e.g. out-parameters). Assigning through
  such a reference would silently retarget the owner — compile error instead.
- Stealing ownership: `proxy_ptr<T> p = std::move(owner);` makes an observer copy;
  `owner` stays the owner and can still `proxy_delete()`.

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

// AFTER
obj.proxy_delete();             // stack/member object: expires its proxy_from_this() observers
owner_ptr.proxy_delete();       // make_proxy object: frees memory + expires every observer

// NEVER: owner_ptr->proxy_delete() or observer->proxy_delete()
// It compiles (it reaches the object's proxy_parent_base::proxy_delete()), but on a
// make_proxy object it only expires the proxy_from_this() observers: observers taken
// from the owner stay alive() and the object is never freed.
```

### 6. Up-casts are implicit

```cpp
// BEFORE
f_takes_base(derived->proxy_from_this());               // or static_pointer_cast<Base>(derived)

// AFTER — proxy_ptr<Derived> and proxy_owner_ptr<Derived> convert to proxy_ptr<Base>
f_takes_base(derived);                                  // address-adjusted for multiple inheritance
```

Down-casts stay explicit (`static_pointer_cast` / `dynamic_pointer_cast`). The old explicit
forms still compile, so existing up-casts can be removed gradually.

Two limitations, both shared with `std::shared_ptr`:

- **Overloads on base and derived handles.** Passing a `proxy_owner_ptr<Derived>` to
  `f(proxy_ptr<Base>)` / `f(proxy_ptr<Derived>)` is ambiguous: both need a user-defined
  conversion from the owner. Pass the observer you mean: `f(proxy_ptr<Derived>(owner))`.
- **Forward-declared types.** The up-cast check is evaluated once per translation unit. If
  it is first evaluated while `Derived` is only forward-declared, it stays false for that
  translation unit, and a later up-cast fails to compile. Include `Derived`'s definition
  before the first up-cast (a raw `Derived*` -> `Base*` conversion needs it too).

### 7. Handles cannot be built from raw pointers

```cpp
// BEFORE — compiled, and created a second owning control block
proxy_ptr<Foo> p(raw_foo);
set_of_handles.emplace(this);

// AFTER — compile error; use the owner, This() / proxy_from_this(), or make_proxy
```

Only `proxy_owner_ptr` (and so `make_proxy`) can build a handle from a raw pointer. A raw-pointer
observer would have owned a second control block and deleted the object behind its real owner.

### 8. LP* typedefs pattern (game server / manager pattern)

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
| `proxy_ptr<T> p = make_proxy<T>()`  | unchanged — implicit observer conversion   | no owner left: object lives until last observer drops |
| `void f(proxy_ptr<T>& out)` + owner | pass an observer, or take `const&` / value | owners don't bind to `proxy_ptr<T>&` |
| `auto copy = owner`                 | `proxy_ptr<T> copy = owner`               | owner is move-only           |
| `copy.proxy_delete()`               | `owner.proxy_delete()`                     | only owner can delete        |
| `cast_result.proxy_delete()`        | `owner.proxy_delete()`                     | casts return observers       |
| `proxy_from_this().proxy_delete()`  | `obj.proxy_delete()` or `owner.proxy_delete()` | was broken before anyway |
| `owner->proxy_delete()`             | `owner.proxy_delete()`                     | `->` frees nothing on make_proxy objects |
| `f(derived->proxy_from_this())`     | `f(derived)`                               | up-casts are implicit        |
| `proxy_ptr<T> p(raw)` / `emplace(this)` | owner / `This()` / `make_proxy`        | raw construction is owner-only |
| container `it->second.proxy_delete()`| store `proxy_owner_ptr<T>` in container   | same call, different type    |
| `observer.proxy_release()`          | `owner.proxy_release()`                    | only owner can release       |
