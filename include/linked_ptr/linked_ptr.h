#include <type_traits>
#include <functional>
#include <assert.h>
#include <unordered_set>

namespace linked {
    template <class _Ty> struct linked_default_delete {
        constexpr linked_default_delete() noexcept = default;

        template <class _Ty2,
                  std::enable_if_t<std::is_convertible_v<_Ty2*, _Ty*>, int> = 0>
        linked_default_delete(const linked_default_delete<_Ty2>&) noexcept {}

        void operator()(_Ty* _Ptr) const noexcept {
            static_assert(0 < sizeof(_Ty), "can't delete an incomplete type");
            delete _Ptr;
        }
    };

    template <class _Ty> struct linked_default_delete<_Ty[]> {
        constexpr linked_default_delete() noexcept = default;

        template <class _Uty,
                  std::enable_if_t<std::is_convertible_v<_Uty (*)[], _Ty (*)[]>,
                                   int> = 0>
        linked_default_delete(const linked_default_delete<_Uty[]>&) noexcept {}

        template <class _Uty,
                  std::enable_if_t<std::is_convertible_v<_Uty (*)[], _Ty (*)[]>,
                                   int> = 0>
        void operator()(_Uty* _Ptr) const noexcept {
            static_assert(0 < sizeof(_Uty), "can't delete an incomplete type");
            delete[] _Ptr;
        }
    };

    namespace detail {
        template <class _Ty, class _PtrClass,
                  class _Dx = linked_default_delete<_Ty>>
        class _linked_node {
           public:
            using pointer = _Ty*;
            using object_t = _Ty;
            using nodeptr_t = _PtrClass*;
            using deleter_t = _Dx;
            using notifier_t = std::function<void(const _PtrClass&)>;
            using nodeset_t = std::unordered_set<nodeptr_t>;

           public:
            void set_notifier(const notifier_t& notifier) {
                if (!this->_notifier)
                    this->_notifier = new notifier_t;
                *this->_notifier = notifier;
            }

           protected:
            nodeptr_t _this_node() { return static_cast<nodeptr_t>(this); }

            bool _is_head() const { return this == this->_head; }
            bool _is_orphan() const {
                return _is_head() &&
                       (!this->_node_set || this->_node_set->empty());
            }

            void _set_ptr(pointer ptr) { this->_ptr = ptr; }
            void _set_head(nodeptr_t ptr) { this->_head = ptr; }
            void _set_nodes(nodeset_t* ptr) { this->_node_set = ptr; }

            void _add_child(nodeptr_t child) {
                assert(child);
                assert(this->_head);

                if (this == child)
                    return;

                if (_is_head())
                    _allocate_set();

                if (!this->_node_set->emplace(child).second)
                    return;

                child->_set_ptr(this->_ptr);
                child->_set_head(this->_head);
                child->_set_nodes(this->_node_set);
            }

            nodeptr_t _get_head() const { return this->_head; }
            deleter_t _get_deleter() { return deleter_t(); }

            void _detach(nodeptr_t newptr = nullptr) {
                if (this == newptr)
                    return;

                if (this->_node_set)
                    this->_node_set->erase(_this_node());

                if (_is_orphan() && !newptr) {
                    _delete_ptr();
                    _delete_node_set();
                    return;
                }

                if (_is_head()) {
                    _allocate_set();
                    newptr = newptr ? newptr : _get_next_head();
                    newptr->_set_nodes(this->_node_set);
                    newptr->_set_ptr(this->_ptr);
                    this->_node_set->emplace(newptr);
                    _on_each_node([&](auto node) { node->_set_head(newptr); });
                }

                else {
                    this->_node_set->erase(_this_node());
                    this->_ptr = nullptr;
                    this->_node_set = nullptr;
                    this->_head = _this_node();
                }
            }

            void _on_each_node(std::function<void(nodeptr_t)> Func) {
                if (this->_node_set) {
                    for (auto& node : *this->_node_set)
                        Func(node);
                }
            }

            void _notify() {
                if (this->_notifier)
                    (*this->_notifier)(*static_cast<nodeptr_t>(this));
            }

            void _delete_ptr() {
                if (this->_ptr) {
                    _get_deleter()(this->_ptr);
                    this->_ptr = nullptr;
                }
            }

            void _delete_node_set() {
                if (this->_node_set) {
                    delete (this->_node_set);
                    this->_node_set = nullptr;
                }
            }

            void _linked_assign(pointer ptr) {
                _delete_ptr();
                _on_each_node([&](auto node) {
                    node->set_ptr(ptr);
                    node->_notify();
                });
            }

            void _linked_delete() {
                _delete_ptr();
                _on_each_node([](auto node) {
                    node->_set_ptr(nullptr);
                    node->_notify();
                });
            }

            pointer _linked_release() {
                auto old = this->_ptr;
                if (old) {
                    _on_each_node([](auto node) {
                        node->set_ptr(nullptr);
                        node->_notify();
                    });
                }
                return old;
            }

            nodeptr_t _get_next_head() { return *this->_node_set->begin(); }

            void _allocate_set() {
                if (_is_head() && !this->_node_set)
                    this->_node_set = new nodeset_t;
            }

            void _set_orphan(pointer val) {
                this->_head = _this_node();
                this->_ptr = val;
            }

            void _delete_notifier() {
                if (this->_notifier) {
                    delete (this->_notifier);
                    this->_notifier = nullptr;
                }
            }

           protected:
            mutable nodeptr_t _head = nullptr;
            mutable nodeset_t* _node_set = nullptr;
            pointer _ptr = nullptr;
            notifier_t* _notifier = nullptr;
        };
    }  // namespace detail

    template <class _Ty>
    class linked_ptr : public detail::_linked_node<_Ty, linked_ptr<_Ty>> {
       private:
        using super_t = detail::_linked_node<_Ty, linked_ptr<_Ty>>;
        using pointer = typename super_t::pointer;
        using deleter_t = typename super_t::deleter_t;
        using noderef_t = linked_ptr<_Ty>&;
        using moveref_t = linked_ptr<_Ty>&&;

       public:
        linked_ptr() { super_t::_set_orphan(nullptr); }
        linked_ptr(pointer ptr) { super_t::_set_orphan(ptr); }
        linked_ptr(const noderef_t parent) { parent._add_child(this); }
        linked_ptr(moveref_t other) { other._detach(this); }
        linked_ptr& operator=(const noderef_t parent) {
            parent._add_child(this);
            return *this;
        }
        linked_ptr& operator=(moveref_t other) { other._detach(this); }
        linked_ptr& operator=(nullptr_t) noexcept {
            detach();
            return *this;
        }
        pointer operator->() const { return get(); }
        std::add_lvalue_reference_t<_Ty> operator*() const { return get(); }
        std::add_lvalue_reference_t<_Ty> operator[](std::size_t s) const {
            return this->_ptr[s];
        };
        operator bool() const { return (this->_ptr); }
        explicit operator pointer() const { return this->_ptr; }
        pointer ptr() const { return this->_ptr; }
        pointer get() const { return this->_ptr; }
        void linked_assign(pointer ptr) { super_t::_linked_assign(ptr); }
        void linked_delete() { super_t::_linked_delete(); }
        void detach() { super_t::_detach(); }
        [[nodiscard]] pointer linked_release() {
            return super_t::_linked_release();
        }
        ~linked_ptr() {
            detach();
            super_t::_delete_notifier();
        }
    };

    template <class _Ty>
    class linked_ptr<_Ty[]>
        : public detail::_linked_node<_Ty, linked_ptr<_Ty[]>,
                                      linked_default_delete<_Ty[]>> {
       private:
        using super_t = detail::_linked_node<_Ty, linked_ptr<_Ty[]>,
                                             linked_default_delete<_Ty[]>>;
        using pointer = typename super_t::pointer;
        using deleter_t = typename super_t::deleter_t;
        using noderef_t = linked_ptr<_Ty[]>&;
        using moveref_t = linked_ptr<_Ty[]>&&;
        using element_t = _Ty;

        template <class _Uty>
        using _Enable_ctor_reset = std::enable_if_t<
            std::is_same_v<_Uty, pointer> || std::is_same_v<_Uty, nullptr_t> ||
            std::is_convertible_v<std::remove_pointer_t<_Uty> (*)[],
                                  element_t (*)[]>>;

       public:
        linked_ptr() { super_t::_set_orphan(nullptr); }
        template <class _Ty2, class trait = _Enable_ctor_reset<_Ty2>>
        linked_ptr(_Ty2 ptr) {
            super_t::_set_orphan(ptr);
        }
        linked_ptr(const noderef_t parent) { parent._add_child(this); }
        linked_ptr(moveref_t other) { other._detach(this); }
        linked_ptr& operator=(const noderef_t parent) {
            parent._add_child(this);
            return *this;
        }
        linked_ptr& operator=(moveref_t other) { other._detach(this); }
        linked_ptr& operator=(nullptr_t) noexcept {
            detach();
            return *this;
        }
        pointer operator->() const { return get(); }
        operator bool() const { return (this->_ptr); }
        explicit operator pointer() const { return this->_ptr; }
        pointer ptr() const { return this->_ptr; }
        pointer get() const { return this->_ptr; }
        void linked_assign(pointer ptr) { super_t::_linked_assign(ptr); }
        void linked_delete() { super_t::_linked_delete(); }
        void detach() { super_t::_detach(); }
        [[nodiscard]] pointer linked_release() {
            return super_t::_linked_release();
        }
        ~linked_ptr() { detach(); }
    };

    template <class _Ty, class... TyArgs,
              std::enable_if_t<!std::is_array<_Ty>::value, int> = 0>
    [[nodiscard]] linked_ptr<_Ty> make_linked(TyArgs&&... VArgs) {
        return std::move(
            linked_ptr<_Ty>(new _Ty(std::forward<TyArgs>(VArgs)...)));
    }

    template <class _Ty,
              std::enable_if_t<std::is_array_v<_Ty> && std::extent_v<_Ty> == 0,
                               int> = 0>
    [[nodiscard]] linked_ptr<_Ty> make_linked(const std::size_t size) {
        using _Elem = std::remove_extent_t<_Ty>;
        return std::move(linked_ptr<_Ty>(new _Elem[size]));
    }

    template <class _Ty, class = std::enable_if_t<std::extent_v<_Ty> != 0, int>>
    [[nodiscard]] linked_ptr<_Ty> make_linked(const std::size_t) = delete;

}  // namespace linked

template <class _Ty1, class _Ty2>
[[nodiscard]] bool operator==(const linked::linked_ptr<_Ty1>& _Left,
                           const linked::linked_ptr<_Ty2>& _Right) {
    return _Left.get() == _Right.get();
}

template <class _Ty1, class _Ty2>
[[nodiscard]] bool operator!=(const linked::linked_ptr<_Ty1>& _Left,
                           const linked::linked_ptr<_Ty2>& _Right) {
    return !(_Left == _Right);
}

template <class _Ty1, class _Ty2>
[[nodiscard]] bool operator<(const linked::linked_ptr<_Ty1>& _Left,
                          const linked::linked_ptr<_Ty2>& _Right) {
    using _Ptr1 = typename linked::linked_ptr<_Ty1>::pointer;
    using _Ptr2 = typename linked::linked_ptr<_Ty2>::pointer;
    using _Common = std::common_type_t<_Ptr1, _Ptr2>;
    return std::less<_Common>()(_Left.get(), _Right.get());
}

template <class _Ty1, class _Ty2>
[[nodiscard]] bool operator>=(const linked::linked_ptr<_Ty1>& _Left,
                           const linked::linked_ptr<_Ty2>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty1, class _Ty2>
[[nodiscard]] bool operator>(const linked::linked_ptr<_Ty1>& _Left,
                          const linked::linked_ptr<_Ty2>& _Right) {
    return _Right < _Left;
}

template <class _Ty1, class _Ty2>
[[nodiscard]] bool operator<=(const linked::linked_ptr<_Ty1>& _Left,
                           const linked::linked_ptr<_Ty2>& _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
[[nodiscard]] bool operator==(const linked::linked_ptr<_Ty>& _Left,
                           nullptr_t) noexcept {
    return !_Left;
}

template <class _Ty>
[[nodiscard]] bool operator==(nullptr_t,
                           const linked::linked_ptr<_Ty>& _Right) noexcept {
    return !_Right;
}

template <class _Ty>
[[nodiscard]] bool operator!=(const linked::linked_ptr<_Ty>& _Left,
                           nullptr_t _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
[[nodiscard]] bool operator!=(nullptr_t _Left,
                           const linked::linked_ptr<_Ty>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
[[nodiscard]] bool operator<(const linked::linked_ptr<_Ty>& _Left,
                          nullptr_t _Right) {
    using _Ptr = typename linked::linked_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class _Ty>
[[nodiscard]] bool operator<(nullptr_t _Left,
                          const linked::linked_ptr<_Ty>& _Right) {
    using _Ptr = typename linked::linked_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class _Ty>
[[nodiscard]] bool operator>=(const linked::linked_ptr<_Ty>& _Left,
                           nullptr_t _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
[[nodiscard]] bool operator>=(nullptr_t _Left,
                           const linked::linked_ptr<_Ty>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
[[nodiscard]] bool operator>(const linked::linked_ptr<_Ty>& _Left,
                          nullptr_t _Right) {
    return _Right < _Left;
}

template <class _Ty>
[[nodiscard]] bool operator>(nullptr_t _Left,
                          const linked::linked_ptr<_Ty>& _Right) {
    return _Right < _Left;
}

template <class _Ty>
[[nodiscard]] bool operator<=(const linked::linked_ptr<_Ty>& _Left,
                           nullptr_t _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
[[nodiscard]] bool operator<=(nullptr_t _Left,
                           const linked::linked_ptr<_Ty>& _Right) {
    return !(_Right < _Left);
}
