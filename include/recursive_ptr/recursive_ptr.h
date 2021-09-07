#include <type_traits>
#include <functional>

namespace recursive_ptr {
    template <class _Ty> struct recursive_default_delete {
        constexpr recursive_default_delete() noexcept = default;

        template <class _Ty2,
                  std::enable_if_t<std::is_convertible_v<_Ty2*, _Ty*>, int> = 0>
        recursive_default_delete(
            const recursive_default_delete<_Ty2>&) noexcept {}

        void operator()(_Ty* _Ptr) const noexcept {
            static_assert(0 < sizeof(_Ty), "can't delete an incomplete type");
            delete _Ptr;
        }
    };

    template <class _Ty> struct recursive_default_delete<_Ty[]> {
        constexpr recursive_default_delete() noexcept = default;

        template <class _Uty,
                  std::enable_if_t<std::is_convertible_v<_Uty (*)[], _Ty (*)[]>,
                                   int> = 0>
        recursive_default_delete(
            const recursive_default_delete<_Uty[]>&) noexcept {}

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
                  class _Dx = recursive_default_delete<_Ty>>
        class _recursive_node {
           public:
            using pointer = _Ty*;
            using object_t = _Ty;
            using nodeptr_t = _PtrClass*;
            using deleter_t = _Dx;
            using notifier_t = std::function<void(const _PtrClass&)>;

           public:
            void set_notifier(const notifier_t& notifier) {
                this->_notifier = notifier;
            }

           protected:
            nodeptr_t this_node() { return static_cast<nodeptr_t>(this); }

            bool _is_orphan() const { return (this->_parent); }

            void _orphanize_uncheking() const {
                this->_parent = nullptr;
                this->_child = nullptr;
            }

            void _set_parent(nodeptr_t parent) {
                if (this->_parent)
                    this->_parent->_remove_child();
                this->_parent = parent;
            }

            void _set_ptr(pointer ptr) { this->_ptr = ptr; }

            void _remove_child() const {
                if (this->_child)
                    this->_child = this->_child->_get_child();
            }

            void _add_child(nodeptr_t child) {
                if (this == child)
                    return;

                if (this->_child)
                    this->_child->_add_child(child);

                else {
                    this->_child = child;
                    if (child) {
                        child->_set_parent(this_node());
                        child->_set_ptr(this->_ptr);
                    }
                }
            }

            void _replace_child(nodeptr_t child) {
                this->_child = nullptr;
                _add_child(child);
            }

            nodeptr_t _get_parent() const { return this->_parent; }

            nodeptr_t _get_child() const { return this->_child; }

            deleter_t& _get_deleter() { return this->_deleter; }

            const notifier_t& _get_notifier() const { return this->_notifier; }

            void _detach(nodeptr_t newptr = nullptr) {
                if (this == newptr)
                    return;
                if (!this->_parent && !this->_child && this->_ptr && !newptr)
                    _delete_ptr();
                auto parent = this->_parent;
                auto child = this->_child;
                _orphanize_uncheking();
                if (child)
                    child->_set_parent(newptr ? newptr : parent);
                if (parent)
                    parent->_replace_child(newptr ? newptr : child);
                if (newptr)
                    newptr->_set_ptr(this->_ptr);

                this->_ptr = nullptr;
            }

            void _recursive_assign_children(pointer ptr) {
                this->_ptr = ptr;
                if (this->_child)
                    this->_recursive_assign_children(ptr);
                _notify();
            }

            void _recursive_assign_parent(pointer ptr) {
                this->_ptr = ptr;
                if (this->_parent)
                    this->_recursive_assign_parent(ptr);
                _notify();
            }

            void _notify() {
                if (this->_notifier)
                    this->_notifier(*static_cast<nodeptr_t>(this));
            }

            bool _delete_ptr() {
                if (this->_ptr) {
                    _get_deleter()(this->_ptr);
                    this->_ptr = nullptr;
                    return true;
                }

                return false;
            }

            void _recursive_assign(pointer ptr) {
                _delete_ptr();
                this->_ptr = ptr;
                if (this->_child)
                    this->_child->_recursive_assign_children(ptr);
                if (this->_parent)
                    this->_parent->_recursive_assign_parent(ptr);
                _notify();
            }

            void _recursive_delete() {
                _delete_ptr();
                if (this->_child)
                    this->_child->_recursive_assign_children(nullptr);
                if (this->_parent)
                    this->_parent->_recursive_assign_parent(nullptr);
                _notify();
            }

            pointer _recursive_release() {
                auto old = this->_ptr;
                if (old) {
                    this->_ptr = nullptr;
                    if (this->_child)
                        this->_child->_recursive_assign_children(nullptr);
                    if (this->_parent)
                        this->_parent->_recursive_assign_parent(nullptr);
                    _notify();
                }
                return old;
            }

           protected:
            mutable nodeptr_t _parent = nullptr;
            mutable nodeptr_t _child = nullptr;
            pointer _ptr = nullptr;
            deleter_t _deleter = {};
            notifier_t _notifier = {};
        };
    }  // namespace detail

    template <class _Ty>
    class recursive_ptr
        : public detail::_recursive_node<_Ty, recursive_ptr<_Ty>> {
       private:
        using super_t = detail::_recursive_node<_Ty, recursive_ptr<_Ty>>;
        using pointer = typename super_t::pointer;
        using deleter_t = typename super_t::deleter_t;
        using noderef_t = recursive_ptr<_Ty>&;
        using moveref_t = recursive_ptr<_Ty>&&;

       public:
        recursive_ptr() {}
        recursive_ptr(pointer ptr) { this->_ptr = ptr; }
        recursive_ptr(const noderef_t parent) { parent._add_child(this); }
        recursive_ptr(moveref_t other) { other._detach(this); }
        recursive_ptr(pointer ptr, deleter_t deleter) {
            super_t::set_ptr(ptr);
            this->_deleter = deleter;
        }
        recursive_ptr& operator=(const noderef_t parent) {
            parent._add_child(this);
            return *this;
        }
        recursive_ptr& operator=(moveref_t other) { other._detach(this); }
        recursive_ptr& operator=(nullptr_t) noexcept {
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
        void recursive_assign(pointer ptr) { super_t::_recursive_assign(ptr); }
        void recursive_delete() { super_t::_recursive_delete(); }
        void detach() { super_t::_detach(); }
        _NODISCARD pointer recursive_release() {
            return super_t::_recursive_release();
        }
        ~recursive_ptr() { detach(); }
    };

    template <class _Ty>
    class recursive_ptr<_Ty[]>
        : public detail::_recursive_node<_Ty, recursive_ptr<_Ty[]>,
                                         recursive_default_delete<_Ty[]>> {
       private:
        using super_t =
            detail::_recursive_node<_Ty, recursive_ptr<_Ty[]>,
                                    recursive_default_delete<_Ty[]>>;
        using pointer = typename super_t::pointer;
        using deleter_t = typename super_t::deleter_t;
        using noderef_t = recursive_ptr<_Ty[]>&;
        using moveref_t = recursive_ptr<_Ty[]>&&;
        using element_t = _Ty;

        template <class _Uty>
        using _Enable_ctor_reset = std::enable_if_t<
            std::is_same_v<_Uty, pointer> || std::is_same_v<_Uty, nullptr_t> ||
            std::is_convertible_v<std::remove_pointer_t<_Uty> (*)[],
                                  element_t (*)[]>>;

       public:
        recursive_ptr() {}
        template <class _Ty2, class trait = _Enable_ctor_reset<_Ty2>>
        recursive_ptr(_Ty2 ptr) {
            this->_ptr = ptr;
        }
        recursive_ptr(const noderef_t parent) { parent._add_child(this); }
        recursive_ptr(moveref_t other) { other._detach(this); }
        template <class _Ty2, class trait = _Enable_ctor_reset<_Ty2>>
        recursive_ptr(_Ty2 ptr, deleter_t deleter) {
            this->_ptr = ptr;
            this->_deleter = deleter;
        }
        recursive_ptr& operator=(const noderef_t parent) {
            parent._add_child(this);
            return *this;
        }
        recursive_ptr& operator=(moveref_t other) { other._detach(this); }
        recursive_ptr& operator=(nullptr_t) noexcept {
            detach();
            return *this;
        }
        pointer operator->() const { return get(); }
        operator bool() const { return (this->_ptr); }
        explicit operator pointer() const { return this->_ptr; }
        pointer ptr() const { return this->_ptr; }
        pointer get() const { return this->_ptr; }
        void recursive_assign(pointer ptr) { super_t::_recursive_assign(ptr); }
        void recursive_delete() { super_t::_recursive_delete(); }
        void detach() { super_t::_detach(); }
        _NODISCARD pointer recursive_release() {
            return super_t::_recursive_release();
        }
        ~recursive_ptr() { detach(); }
    };

    template <class _Ty, class... TyArgs,
              std::enable_if_t<!std::is_array<_Ty>::value, int> = 0>
    _NODISCARD recursive_ptr<_Ty> make_recursive(TyArgs&&... VArgs) {
        return std::move(
            recursive_ptr<_Ty>(new _Ty(std::forward<TyArgs>(VArgs)...)));
    }

    template <class _Ty,
              std::enable_if_t<std::is_array_v<_Ty> && std::extent_v<_Ty> == 0,
                               int> = 0>
    _NODISCARD recursive_ptr<_Ty> make_recursive(const std::size_t size) {
        using _Elem = std::remove_extent_t<_Ty>;
        return std::move(recursive_ptr<_Ty>(new _Elem[size]));
    }

    template <class _Ty, class = std::enable_if_t<std::extent_v<_Ty> != 0, int>>
    _NODISCARD recursive_ptr<_Ty> make_recursive(const std::size_t) = delete;

}  // namespace recursive_ptr

template <class _Ty1, class _Ty2>
_NODISCARD bool operator==(const recursive_ptr::recursive_ptr<_Ty1>& _Left,
                           const recursive_ptr::recursive_ptr<_Ty2>& _Right) {
    return _Left.get() == _Right.get();
}

template <class _Ty1, class _Ty2>
_NODISCARD bool operator!=(const recursive_ptr::recursive_ptr<_Ty1>& _Left,
                           const recursive_ptr::recursive_ptr<_Ty2>& _Right) {
    return !(_Left == _Right);
}

template <class _Ty1, class _Ty2>
_NODISCARD bool operator<(const recursive_ptr::recursive_ptr<_Ty1>& _Left,
                          const recursive_ptr::recursive_ptr<_Ty2>& _Right) {
    using _Ptr1 = typename recursive_ptr::recursive_ptr<_Ty1>::pointer;
    using _Ptr2 = typename recursive_ptr::recursive_ptr<_Ty2>::pointer;
    using _Common = std::common_type_t<_Ptr1, _Ptr2>;
    return less<_Common>()(_Left.get(), _Right.get());
}

template <class _Ty1, class _Ty2>
_NODISCARD bool operator>=(const recursive_ptr::recursive_ptr<_Ty1>& _Left,
                           const recursive_ptr::recursive_ptr<_Ty2>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty1, class _Ty2>
_NODISCARD bool operator>(const recursive_ptr::recursive_ptr<_Ty1>& _Left,
                          const recursive_ptr::recursive_ptr<_Ty2>& _Right) {
    return _Right < _Left;
}

template <class _Ty1, class _Ty2>
_NODISCARD bool operator<=(const recursive_ptr::recursive_ptr<_Ty1>& _Left,
                           const recursive_ptr::recursive_ptr<_Ty2>& _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
_NODISCARD bool operator==(const recursive_ptr::recursive_ptr<_Ty>& _Left,
                           nullptr_t) noexcept {
    return !_Left;
}

template <class _Ty>
_NODISCARD bool operator==(
    nullptr_t, const recursive_ptr::recursive_ptr<_Ty>& _Right) noexcept {
    return !_Right;
}

template <class _Ty>
_NODISCARD bool operator!=(const recursive_ptr::recursive_ptr<_Ty>& _Left,
                           nullptr_t _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
_NODISCARD bool operator!=(
    nullptr_t _Left, const recursive_ptr::recursive_ptr<_Ty>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
_NODISCARD bool operator<(const recursive_ptr::recursive_ptr<_Ty>& _Left,
                          nullptr_t _Right) {
    using _Ptr = typename unique_ptr<_Ty, _Dx>::pointer;
    return less<_Ptr>()(_Left.get(), _Right);
}

template <class _Ty>
_NODISCARD bool operator<(nullptr_t _Left,
                          const recursive_ptr::recursive_ptr<_Ty>& _Right) {
    using _Ptr = typename unique_ptr<_Ty, _Dx>::pointer;
    return less<_Ptr>()(_Left, _Right.get());
}

template <class _Ty>
_NODISCARD bool operator>=(const recursive_ptr::recursive_ptr<_Ty>& _Left,
                           nullptr_t _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
_NODISCARD bool operator>=(nullptr_t _Left,
                           const recursive_ptr::recursive_ptr<_Ty>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
_NODISCARD bool operator>(const recursive_ptr::recursive_ptr<_Ty>& _Left,
                          nullptr_t _Right) {
    return _Right < _Left;
}

template <class _Ty>
_NODISCARD bool operator>(nullptr_t _Left,
                          const recursive_ptr::recursive_ptr<_Ty>& _Right) {
    return _Right < _Left;
}

template <class _Ty>
_NODISCARD bool operator<=(const recursive_ptr::recursive_ptr<_Ty>& _Left,
                           nullptr_t _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
_NODISCARD bool operator<=(nullptr_t _Left,
                           const recursive_ptr::recursive_ptr<_Ty>& _Right) {
    return !(_Right < _Left);
}
