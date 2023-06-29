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
#pragma once
#ifndef __PROXY_PROXY_PTR_H__
    #define __PROXY_PROXY_PTR_H__

    #include <type_traits>
    #include <assert.h>
    #include <atomic>
    #include <memory>

    #define PROXY_PTR_NO_DISCARD [[nodiscard]]
    #define PROXY_PTR_UNUSED(v) ((void)v)
    #if __cplusplus >= 201703L
        #define PROXY_PTR_IS_ARRAY(type) std::is_array_v<type>
        #define PROXY_PTR_EXTENT(type) std::extent_v<type>
        #define PROXY_PTR_CONSTEXPR(expr) constexpr(expr)
    #else
        #define PROXY_PTR_IS_ARRAY(type) std::is_array<type>::value
        #define PROXY_PTR_EXTENT(type) std::extent<type>::value
        #define PROXY_PTR_CONSTEXPR(expr) (expr)
    #endif

namespace proxy {
    struct proxy_atomic {};
    struct proxy_non_atomic {};

    // forward declaration
    template <class Ty> class proxy_parent_base;

    namespace detail {
        template <class... args> using void_t = void;

        template <class _Fx, class _Arg, class = void>
        struct _can_call_function_object : std::false_type {};
        template <class _Fx, class _Arg>
        struct _can_call_function_object<
            _Fx, _Arg,
            void_t<decltype(std::declval<_Fx>()(std::declval<_Arg>()))>>
            : std::true_type {};

        template <class Ty> struct _deduce_ref_count_type;
        template <> struct _deduce_ref_count_type<proxy_atomic> {
            using type = std::atomic<size_t>;
        };
        template <> struct _deduce_ref_count_type<proxy_non_atomic> {
            using type = size_t;
        };

        template <class Ty>
        using deduce_ref_count_type = typename _deduce_ref_count_type<Ty>::type;

        template <class Type, class AtomicType> class _proxy_common_state_base {
           protected:
            using ref_count_t = deduce_ref_count_type<AtomicType>;
            Type* _ptr = nullptr;
            ref_count_t _ref_count = static_cast<size_t>(0);
            bool _alive = false;

           public:
            _proxy_common_state_base(Type* p) : _ptr(p) { _alive = true; }

            void inc_ref() { _ref_count++; }
            bool dec_ref() {
                if (_ref_count == 0)
                    return false;
                return --_ref_count != 0;
            }

            bool alive() const { return _alive; }
            Type* get() const { return _ptr; }
            Type* release() {
                _alive = false;
                return _ptr;
            }

            virtual ~_proxy_common_state_base() {}
        };

        template <class Type, class Dex, class AtomicType>
        class _proxy_common_state
            : private Dex,
              public _proxy_common_state_base<Type, AtomicType> {
           public:
            _proxy_common_state(Type* ptr)
                : _proxy_common_state_base<Type, AtomicType>(ptr) {}
            _proxy_common_state(Type* ptr, const Dex& dx)
                : _proxy_common_state_base<Type, AtomicType>(ptr) {
                static_cast<Dex&>(*this) = dx;
            }

            void delete_ptr() {
                if (this->_ptr && this->_alive) {
                    static_cast<Dex&>(*this)(this->_ptr);
                    this->_alive = false;
                }
            }
            virtual ~_proxy_common_state() { delete_ptr(); }
        };

        template <class Ty> struct _extract_proxy_pointer_type {
            using type = Ty*;
        };
        template <class Ty> struct _extract_proxy_pointer_type<Ty[]> {
            using type = Ty*;
        };

        template <class Ty>
        using extract_proxy_pointer_type =
            typename _extract_proxy_pointer_type<Ty>::type;

        template <class Ty>
        using extract_proxy_type =
            std::remove_pointer_t<extract_proxy_pointer_type<Ty>>;

        template <class Ty>
        constexpr bool is_proxy_valid_type =
            !PROXY_PTR_IS_ARRAY(Ty) ||
            (PROXY_PTR_IS_ARRAY(Ty) && PROXY_PTR_EXTENT(Ty) == 0);

        template <class Type, class Dex>
        constexpr bool is_valid_deleter =
            std::is_move_constructible<Dex>::value &&
            detail::_can_call_function_object<Dex&, Type*&>::value;

        template <class Ty>
        constexpr bool is_valid_atomic_flag =
            std::is_same<Ty, proxy_atomic>::value ||
            std::is_same<Ty, proxy_non_atomic>::value;

        template <class Ty>
        using enable_valid_atomic_flag =
            std::enable_if_t<is_valid_atomic_flag<Ty>>;
    }  // namespace detail

    template <class _RTy, class AtomicTypeFlag = proxy_non_atomic,
              class = detail::enable_valid_atomic_flag<AtomicTypeFlag>>
    class proxy_ptr {
        using Type = detail::extract_proxy_type<_RTy>;
        using _common_PtrType =
            detail::_proxy_common_state_base<Type, AtomicTypeFlag>;

       protected:
        proxy_ptr(_common_PtrType* _ptr) {
            _ppobj = _ptr;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       public:
        proxy_ptr() {}
        proxy_ptr(std::nullptr_t) {}
        proxy_ptr(const proxy_ptr& n) { _proxy_from(n); }
        explicit proxy_ptr(Type* r) {
            using deleter_type = std::default_delete<Type>;
            using common_ptr_type =
                detail::_proxy_common_state<Type, deleter_type, AtomicTypeFlag>;
            _detach(new common_ptr_type(r));
        }
        template <class Dex, std::enable_if_t<
                                 detail::is_valid_deleter<Type, Dex>, int> = 0>
        explicit proxy_ptr(Type* r, const Dex& dx) {
            using common_ptr_type =
                detail::_proxy_common_state<Type, Dex, AtomicTypeFlag>;
            _detach(new common_ptr_type(r, dx));
        }

        explicit operator bool() const { return alive(); }
        explicit operator Type*() const { return get(); }

        template <class Type2, class AtomicType2>
        PROXY_PTR_NO_DISCARD bool operator==(
            const proxy::proxy_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return get() == _Right.get();
        }

        template <class Type2, class AtomicType2>
        PROXY_PTR_NO_DISCARD bool operator!=(
            const proxy::proxy_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return !(*this == _Right);
        }

        template <class Type2, class AtomicType2>
        PROXY_PTR_NO_DISCARD bool operator<(
            const proxy::proxy_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return get() < _Right.get();
        }

        template <class Type2, class AtomicType2>
        PROXY_PTR_NO_DISCARD bool operator>=(
            const proxy::proxy_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return !(*this < _Right);
        }

        template <class Type2, class AtomicType2>
        PROXY_PTR_NO_DISCARD bool operator>(
            const proxy::proxy_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return _Right < *this;
        }

        template <class Type2, class AtomicType2>
        PROXY_PTR_NO_DISCARD bool operator<=(
            const proxy::proxy_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return !(_Right < *this);
        }

        Type* get() const {
            if (!_is_Pointing())
                return nullptr;
            return _ppobj->get();
        }

        Type* ptr() const { return get(); }

        template <class Type2 = Type,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(Type2)>>
        Type2* operator->() const {
            assert(_is_Pointing());
            return get();
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<PROXY_PTR_IS_ARRAY(Type2)>>
        Type2& operator[](std::ptrdiff_t p) const {
            assert(_is_Pointing());
            return (*get())[p];
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(Type2)>>
        Type2& operator*() const {
            assert(_is_Pointing());
            return *get();
        }

        decltype(auto) operator=(const proxy_ptr<Type, AtomicTypeFlag>& r) {
            _detach(r._ppobj);
            return (*this);
        }

        decltype(auto) operator=(std::nullptr_t) {
            _detach();
            return (*this);
        }

        Type* proxy_release() {
            if (!_is_Pointing())
                return nullptr;
            return _ppobj->release();
        }

        void proxy_delete() {
            if (_is_Pointing())
                _ppobj->delete_ptr();
        }

        bool alive() const {
            return _is_Pointing() && _ppobj->alive() && _ppobj->get();
        }

        ~proxy_ptr() { _detach(); }

       protected:
        void _proxy_from(const proxy_ptr& n) {
            if (n._ppobj == _ppobj)
                return;
            _detach(n._ppobj);
        }
        bool _is_Pointing() const { return _ppobj != nullptr; }
        void _detach(_common_PtrType* n = nullptr) {
            if (_ppobj)
                if (!_ppobj->dec_ref())
                    delete (_ppobj);

            _ppobj = n;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       private:
        _common_PtrType* _ppobj = nullptr;
    };

    template <class Type> class proxy_parent_base {
       public:
        proxy_ptr<Type> proxy() { return {_proxyPtr}; }
        void proxy_delete() {
            auto ret = _proxyPtr.proxy_release();
            _proxyPtr = static_cast<Type*>(this);
        }
        virtual ~proxy_parent_base() {
            auto ret = _proxyPtr.proxy_release();
            PROXY_PTR_UNUSED(ret);
        }

       private:
        proxy_ptr<Type> _proxyPtr{static_cast<Type*>(this)};
    };

    namespace detail {
        template <class Ty, class Atomic> struct make_proxy {
            template <class... args>
            static proxy_ptr<Ty, Atomic> construct(const args&... va) {
                return proxy_ptr<Ty, Atomic>{new Ty(va...)};
            }
        };

        template <class Ty, class Atomic> struct make_proxy<Ty[], Atomic> {
            static proxy_ptr<Ty[], Atomic> construct(size_t len) {
                return proxy_ptr<Ty[], Atomic>{new Ty[len]};
            }
        };
    }  // namespace detail

    template <class Ty, class... Args>
    std::enable_if_t<detail::is_proxy_valid_type<Ty>, proxy_ptr<Ty>> make_proxy(
        const Args&... Arguments) {
        return detail::make_proxy<Ty, proxy_non_atomic>::construct(
            Arguments...);
    }

    template <class Ty, class... Args>
    std::enable_if_t<detail::is_proxy_valid_type<Ty>,
                     proxy_ptr<Ty, proxy_atomic>>
    make_proxy_atomic(const Args&... Arguments) {
        return detail::make_proxy<Ty, proxy_atomic>::construct(Arguments...);
    }

    template <class Type, class AtomicType> struct proxy_factory {
        template <class... args>
        static proxy::proxy_ptr<Type, AtomicType> make(const args&... arg) {
            return detail::make_proxy<Type, AtomicType>::construct(arg...);
        }
    };

}  // namespace proxy

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator==(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, std::nullptr_t) noexcept {
    return !_Left;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator==(
    std::nullptr_t, const proxy::proxy_ptr<Type, AtomicType>& _Right) noexcept {
    return !_Right;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator!=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left,
    std::nullptr_t _Right) noexcept {
    return !(_Left == _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator!=(
    std::nullptr_t _Left,
    const proxy::proxy_ptr<Type, AtomicType>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<(
    const proxy::proxy_ptr<Type, AtomicType>& _Left,
    std::nullptr_t _Right) noexcept {
    using _Ptr = typename proxy::proxy_ptr<Type, AtomicType>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<(
    std::nullptr_t _Left,
    const proxy::proxy_ptr<Type, AtomicType>& _Right) noexcept {
    using _Ptr = typename proxy::proxy_ptr<Type, AtomicType>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left,
    std::nullptr_t _Right) noexcept {
    return !(_Left < _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>=(
    std::nullptr_t _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return !(_Left < _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, std::nullptr_t _Right) {
    return _Right < _Left;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>(
    std::nullptr_t _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return _Right < _Left;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, std::nullptr_t _Right) {
    return !(_Right < _Left);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<=(
    std::nullptr_t _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return !(_Right < _Left);
}

//

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator==(
    const proxy::proxy_ptr<Type, AtomicType>& _Left,
    const Type* const _ptr) noexcept {
    return _Left.get() == _ptr;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator==(
    const Type* _ptr,
    const proxy::proxy_ptr<Type, AtomicType>& _Right) noexcept {
    return _Right.get() == _ptr;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator!=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left,
    const Type* const _Right) noexcept {
    return !(_Left == _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator!=(
    const Type* _Left,
    const proxy::proxy_ptr<Type, AtomicType>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, const Type* const _Right) {
    using _Ptr = typename proxy::proxy_ptr<Type, AtomicType>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<(
    const Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    using _Ptr = typename proxy::proxy_ptr<Type, AtomicType>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, const Type* const _Right) {
    return !(_Left < _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>=(
    const Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return !(_Left < _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, const Type* const _Right) {
    return _Right < _Left;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>(
    const Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return _Right < _Left;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, const Type* const _Right) {
    return !(_Right < _Left);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<=(
    const Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return !(_Right < _Left);
}

//

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator==(
    const proxy::proxy_ptr<Type, AtomicType>& _Left,
    Type* const _ptr) noexcept {
    return _Left.get() == _ptr;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator==(
    Type* const _ptr,
    const proxy::proxy_ptr<Type, AtomicType>& _Right) noexcept {
    return _Right.get() == _ptr;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator!=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left,
    Type* const _Right) noexcept {
    return !(_Left == _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator!=(
    Type* const _Left,
    const proxy::proxy_ptr<Type, AtomicType>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, Type* const _Right) {
    using _Ptr = typename proxy::proxy_ptr<Type, AtomicType>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<(
    Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    using _Ptr = typename proxy::proxy_ptr<Type, AtomicType>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, Type* const _Right) {
    return !(_Left < _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>=(
    Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return !(_Left < _Right);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, Type* const _Right) {
    return _Right < _Left;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator>(
    Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return _Right < _Left;
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<=(
    const proxy::proxy_ptr<Type, AtomicType>& _Left, Type* const _Right) {
    return !(_Right < _Left);
}

template <class Type, class AtomicType>
PROXY_PTR_NO_DISCARD bool operator<=(
    Type* const _Left, const proxy::proxy_ptr<Type, AtomicType>& _Right) {
    return !(_Right < _Left);
}

template <class Type, class AtomicType>
struct std::hash<proxy::proxy_ptr<Type, AtomicType>> {
    size_t operator()(const proxy::proxy_ptr<Type, AtomicType> _ptr) const {
        return reinterpret_cast<std::uintptr_t>(_ptr.get());
    }
};

template <class Type, class AtomicType>
struct std::less<proxy::proxy_ptr<Type, AtomicType>> {
    bool operator()(const proxy::proxy_ptr<Type, AtomicType>& lhs,
                    const proxy::proxy_ptr<Type, AtomicType>& rhs) const {
        return lhs < rhs;
    }
};

#endif
