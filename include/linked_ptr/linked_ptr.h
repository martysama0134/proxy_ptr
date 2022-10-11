///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2022 IkarusDeveloper. All rights reserved.
//
// This code is licensed under the MIT License (MIT).
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef __LINKED_LINKED_PTR_H__
#define __LINKED_LINKED_PTR_H__
#pragma once

#include <type_traits>
#include <functional>
#include <assert.h>
#include <unordered_set>

// This implementation doesn't currently support multithreading
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
            using nodeptr_t = _linked_node*;
            using deleter_t = _Dx;
            using notifier_t = std::function<void(const _PtrClass&)>;
            using nodeset_t = std::unordered_set<nodeptr_t>;

           public:
            void set_notifier(const notifier_t& notifier) {
                if (!this->_notifier)
                    this->_notifier = new notifier_t;
                *this->_notifier = notifier;
            }

            void linked_assign(pointer ptr) {
                _delete_ptr();
                _on_each_node([&](auto node) {
                    node->_set_ptr(ptr);
                    node->_notify();
                });
            }

            void linked_delete() { linked_assign(nullptr); }

            [[nodiscard]] pointer linked_release() {
                auto old = this->_ptr;
                this->_ptr = nullptr;
                linked_assign(nullptr);
                return old;
            }

            void detach(nodeptr_t newptr = nullptr) {
                if (this == newptr)
                    return;
                if (this->_node_set)
                    this->_node_set->erase(this);

                if (_is_orphan() && !newptr) {
                    _delete_ptr();
                    _delete_node_set();
                    return;
                }

                if (_is_head() &&
                    (newptr = newptr ? newptr : _get_next_head())) {
                    _allocate_set();
                    this->_node_set->emplace(newptr);
                    newptr->_set_nodes(this->_node_set);
                    newptr->_set_ptr(this->_ptr);
                    _on_each_node([&](auto node) { node->_set_head(newptr); });
                }

                this->_ptr = nullptr;
                this->_node_set = nullptr;
                this->_head = this;
            }

           protected:
            bool _is_head() const { return this == this->_head; }
            bool _is_orphan() const {
                return _is_head() &&
                       (!this->_node_set || this->_node_set->empty());
            }

            void _set_ptr(pointer ptr) { this->_ptr = ptr; }
            void _set_head(nodeptr_t ptr) { this->_head = ptr; }
            void _set_nodes(nodeset_t* ptr) { this->_node_set = ptr; }

            bool _is_node(nodeptr_t ptr) const {
                return this->_node_set &&
                       this->_node_set->find(ptr) != this->_node_set->end();
            }

            void _add_child(nodeptr_t child) const {
                assert(child);
                assert(this->_head);

                if (this == child)
                    return;
                if (this->_is_node(child))
                    return;

                child->detach();

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

            void _move(nodeptr_t ptr) {
                _add_child(ptr);
                detach();
            }

            void _on_each_node(std::function<void(_PtrClass*)> Func) {
                if (this->_node_set) {
                    for (auto& node : *this->_node_set)
                        Func(static_cast<_PtrClass*>(node));
                }
            }

            void _notify() {
                if (this->_notifier && *this->_notifier)
                    (*this->_notifier)(*static_cast<_PtrClass*>(this));
            }

            void _delete_ptr() {
                if (this->_ptr) {
                    auto old = this->_ptr;
                    this->_ptr = nullptr;
                    _get_deleter()(old);
                }
            }

            void _delete_node_set() {
                if (this->_node_set) {
                    delete (this->_node_set);
                    this->_node_set = nullptr;
                }
            }

            nodeptr_t _get_next_head() {
                return this->_node_set && !this->_node_set->empty()
                           ? *this->_node_set->begin()
                           : nullptr;
            }

            void _allocate_set() const {
                if (_is_head() && !this->_node_set)
                    this->_node_set = new nodeset_t;
            }

            void _set_orphan(pointer val) {
                this->_head = this;
                this->_ptr = val;
                _allocate_set();
            }

            void _delete_notifier() {
                if (this->_notifier) {
                    delete (this->_notifier);
                    this->_notifier = nullptr;
                }
            }

           protected:
            ~_linked_node() {
                detach();
                _delete_notifier();
            }

           protected:
            mutable nodeptr_t _head = this;
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
        using nodeconstref_t = const linked_ptr<_Ty>&;
        using moveref_t = linked_ptr<_Ty>&&;

       public:
        linked_ptr() { super_t::_set_orphan(nullptr); }
        linked_ptr(pointer ptr) { super_t::_set_orphan(ptr); }
        linked_ptr(nodeconstref_t parent) { parent._add_child(this); }
        linked_ptr(moveref_t other) { other._move(this); }
        linked_ptr& operator=(nodeconstref_t parent) {
            parent._add_child(this);
            return *this;
        }
        linked_ptr& operator=(moveref_t other) {
            other._move(this);
            return *this;
        }
        linked_ptr& operator=(std::nullptr_t) noexcept {
            this->detach();
            return *this;
        }
        pointer operator->() const { return get(); }
        std::add_lvalue_reference_t<_Ty> operator*() const { return *get(); }
        operator bool() const { return (this->_ptr); }
        explicit operator pointer() const { return this->_ptr; }
        pointer ptr() const { return this->_ptr; }
        pointer get() const { return this->_ptr; }
        ~linked_ptr() { this->super_t::~super_t(); }
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
        using nodeconstref_t = const linked_ptr<_Ty[]>&;
        using moveref_t = linked_ptr<_Ty[]>&&;
        using element_t = _Ty;

        template <class _Uty>
        using _Enable_ctor_reset = std::enable_if_t<
            std::is_same_v<_Uty, pointer> ||
            std::is_same_v<_Uty, std::nullptr_t> ||
            std::is_convertible_v<std::remove_pointer_t<_Uty> (*)[],
                                  element_t (*)[]>>;

       public:
        linked_ptr() { super_t::_set_orphan(nullptr); }
        template <class _Ty2, class trait = _Enable_ctor_reset<_Ty2>>
        linked_ptr(_Ty2 ptr) {
            super_t::_set_orphan(ptr);
        }
        linked_ptr(nodeconstref_t parent) { parent._add_child(this); }
        linked_ptr(moveref_t other) { other._move(this); }
        linked_ptr& operator=(nodeconstref_t parent) {
            parent._add_child(this);
            return *this;
        }
        linked_ptr& operator=(moveref_t other) {
            other._move(this);
            return *this;
        }
        linked_ptr& operator=(std::nullptr_t) noexcept {
            this->detach();
            return *this;
        }
        pointer operator->() const { return get(); }
        operator bool() const { return (this->_ptr); }
        explicit operator pointer() const { return this->_ptr; }
        pointer ptr() const { return this->_ptr; }
        pointer get() const { return this->_ptr; }
        std::add_lvalue_reference_t<_Ty> operator*() const { return *get(); }
        std::add_lvalue_reference_t<_Ty> operator[](std::size_t s) const {
            return (*this->_ptr)[s];
        };
        ~linked_ptr() { this->super_t::~super_t(); }
    };

    template <class _Ty, class... TyArgs,
              std::enable_if_t<!std::is_array<_Ty>::value, int> = 0>
    [[nodiscard]] linked_ptr<_Ty> make_linked(TyArgs&&... VArgs) {
        return {new _Ty(std::forward<TyArgs>(VArgs)...)};
    }

    template <class _Ty,
              std::enable_if_t<std::is_array_v<_Ty> && std::extent_v<_Ty> == 0,
                               int> = 0>
    [[nodiscard]] linked_ptr<_Ty> make_linked(const std::size_t size) {
        using _Elem = std::remove_extent_t<_Ty>;
        return {new _Elem[size]};
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
                              std::nullptr_t) noexcept {
    return !_Left;
}

template <class _Ty>
[[nodiscard]] bool operator==(std::nullptr_t,
                              const linked::linked_ptr<_Ty>& _Right) noexcept {
    return !_Right;
}

template <class _Ty>
[[nodiscard]] bool operator!=(const linked::linked_ptr<_Ty>& _Left,
                              std::nullptr_t _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
[[nodiscard]] bool operator!=(std::nullptr_t _Left,
                              const linked::linked_ptr<_Ty>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
[[nodiscard]] bool operator<(const linked::linked_ptr<_Ty>& _Left,
                             std::nullptr_t _Right) {
    using _Ptr = typename linked::linked_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class _Ty>
[[nodiscard]] bool operator<(std::nullptr_t _Left,
                             const linked::linked_ptr<_Ty>& _Right) {
    using _Ptr = typename linked::linked_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class _Ty>
[[nodiscard]] bool operator>=(const linked::linked_ptr<_Ty>& _Left,
                              std::nullptr_t _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
[[nodiscard]] bool operator>=(std::nullptr_t _Left,
                              const linked::linked_ptr<_Ty>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
[[nodiscard]] bool operator>(const linked::linked_ptr<_Ty>& _Left,
                             std::nullptr_t _Right) {
    return _Right < _Left;
}

template <class _Ty>
[[nodiscard]] bool operator>(std::nullptr_t _Left,
                             const linked::linked_ptr<_Ty>& _Right) {
    return _Right < _Left;
}

template <class _Ty>
[[nodiscard]] bool operator<=(const linked::linked_ptr<_Ty>& _Left,
                              std::nullptr_t _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
[[nodiscard]] bool operator<=(std::nullptr_t _Left,
                              const linked::linked_ptr<_Ty>& _Right) {
    return !(_Right < _Left);
}

template <class Type> class std::hash<linked::linked_ptr<Type>> {
    auto operator()(const linked::linked_ptr<Type> ptr) { return ptr.get(); }
};

template <class Type> struct std::less<linked::linked_ptr<Type>> {
    bool operator()(const linked::linked_ptr<Type>& lhs,
                    const linked::linked_ptr<Type>& rhs) const {
        return lhs < rhs;
    }
};

#endif
