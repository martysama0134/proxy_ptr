### `linked::linked_ptr`
It's a smart pointer that can edit its children and set a callback function.

### `proxy::proxy_parent_base`
A proxy generator wrapper class. It can be used to wrap a class in order to inherit `proxy_parent_base` methods.

e.g. `proxy_parent_base<std::tuple<int,int,char>>`

It can generate a `proxy_ptr` (child) by using the `.proxy()` method.

### "proxy"
A pointer which doesn't own it's pointed object. The proxy_ptr can be invalidated (setting it to point to nullptr) remotely by its parent (proxy_parent)

### Warning
These classes aren't thread-safe, You shouldn't use them for multithreading.
