# linked_ptr
Smart Pointer that can edit its children and set a callback function
#proxy_parent
A proxy generator wrapper class. it can be used to wrap a class to make it to inherit proxy_parent methods. eg : proxy_parent<std::tuple<int,int,char>>
It can generate a proxy_ptr (child) by using .proxy() method.
# proxy
A pointer which doesn't own it's pointed object. The proxy_ptr can be invalidated (setting it to point to nullptr) remotely by its parent (proxy_parent)


#warning
These classes aren't thread safe, You can't use them with multithreading.
