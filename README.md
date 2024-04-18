### New design version available elsewhere
The new owner_ptr, weak_ptr, proxy_ptr version can be found here: https://github.com/IkarusDeveloper/cake

### `proxy::proxy_parent_base`
A proxy generator wrapper class. It can be used to wrap a class in order to inherit `proxy_parent_base` methods.

e.g. `proxy_parent_base<Obj>`, or alternatively `enable_proxy_from_this<Obj>`

It can generate a `proxy_ptr` (child) by using the `.proxy()` method, or alternatively `.proxy_from_this()`.

It's currently 20 times faster than `std::shared_ptr`.

### "proxy"
A pointer which doesn't own its pointed object. The `proxy_ptr` can be invalidated remotely by its parent (`proxy_parent_base`) if set to `nullptr`.

### Warning
These classes are not thread-safe. If you need multithreading support, check the new version.
