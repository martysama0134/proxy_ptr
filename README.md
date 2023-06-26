### `proxy::proxy_parent_base`
A proxy generator wrapper class. It can be used to wrap a class in order to inherit `proxy_parent_base` methods.

e.g. `proxy_parent_base<std::tuple<int,int,char>>`

It can generate a `proxy_ptr` (child) by using the `.proxy()` method.

It's currently 20 times faster than std::shared_ptr.

### "proxy"
A pointer which doesn't own its pointed object. The `proxy_ptr` can be invalidated remotely by its parent (`proxy_parent_base`) if set to `nullptr`.

### Warning
These classes aren't thread-safe, You shouldn't use them for multithreading.
