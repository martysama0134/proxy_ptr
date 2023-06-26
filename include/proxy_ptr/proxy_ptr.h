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
        template <class _Fx, class _Arg, class = void>
        struct _can_call_function_object : std::false_type {};
        template <class _Fx, class _Arg>
        struct _can_call_function_object<
            _Fx, _Arg,
            std::void_t<decltype(std::declval<_Fx>()(std::declval<_Arg>()))>>
            : std::true_type {};

        template <class Ty> struct _deduce_ref_count_type;
        template <> struct _deduce_ref_count_type<proxy_atomic> {
            using type = std::atomic<size_t>;
        };
        template <> struct _deduce_ref_count_type<proxy_non_atomic> {
            using type = size_t;
        };

        template <class Ty>
        using deduce_ref_count_type = _deduce_ref_count_type<Ty>::type;

        template <class _Ty, class _Atomic> class _proxy_common_state_base {
           protected:
            using ref_count_t = deduce_ref_count_type<_Atomic>;
            _Ty* _ptr = nullptr;
            ref_count_t _ref_count = static_cast<size_t>(0);
            bool _alive = false;

           public:
            _proxy_common_state_base(_Ty* p) : _ptr(p) { _alive = true; }

            void inc_ref() { _ref_count++; }
            bool dec_ref() {
                if (_ref_count == 0)
                    return false;
                return --_ref_count != 0;
            }

            bool alive() const { return _alive; }
            _Ty* get() const { return _ptr; }
            _Ty* release() {
                _alive = false;
                return _ptr;
            }

            virtual ~_proxy_common_state_base() {}
        };

        template <class _Ty, class _Dx, class _Atomic>
        class _proxy_common_state
            : private _Dx,
              public _proxy_common_state_base<_Ty, _Atomic> {
           public:
            _proxy_common_state(_Ty* ptr)
                : _proxy_common_state_base<_Ty, _Atomic>(ptr) {}
            _proxy_common_state(_Ty* ptr, const _Dx& dx)
                : _proxy_common_state_base<_Ty, _Atomic>(ptr) {
                static_cast<_Dx&>(*this) = dx;
            }

            void delete_ptr() {
                if (this->_ptr && this->_alive) {
                    static_cast<_Dx&>(*this)(this->_ptr);
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

        template <class _Ty, class _Dx>
        constexpr bool is_valid_deleter =
            std::is_move_constructible_v<_Dx> &&
            detail::_can_call_function_object<_Dx&, _Ty*&>::value;

        template <class Ty>
        constexpr bool is_valid_atomic_flag =
            std::is_same_v<Ty, proxy_atomic> ||
            std::is_same_v<Ty, proxy_non_atomic>;

        template <class Ty>
        using enable_valid_atomic_flag =
            std::enable_if_t<is_valid_atomic_flag<Ty>>;
    }  // namespace detail

    template <class _RTy, class _AtomicFlag = proxy_non_atomic,
              class = detail::enable_valid_atomic_flag<_AtomicFlag>>
    class proxy_ptr {
        using _Ty = detail::extract_proxy_type<_RTy>;
        using _common_Ptr_Ty =
            detail::_proxy_common_state_base<_Ty, _AtomicFlag>;

       protected:
        proxy_ptr(_common_Ptr_Ty* _ptr) {
            _ppobj = _ptr;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       public:
        proxy_ptr() {}
        proxy_ptr(std::nullptr_t) {}
        proxy_ptr(const proxy_ptr& n) { _proxy_from(n); }
        explicit proxy_ptr(_Ty* r) {
            using deleter_type = std::default_delete<_Ty>;
            using common_ptr_type =
                detail::_proxy_common_state<_Ty, deleter_type, _AtomicFlag>;
            _detach(new common_ptr_type(r));
        }
        template <class _Dx,
                  std::enable_if_t<detail::is_valid_deleter<_Ty, _Dx>, int> = 0>
        explicit proxy_ptr(_Ty* r, const _Dx& dx) {
            using common_ptr_type =
                detail::_proxy_common_state<_Ty, _Dx, _AtomicFlag>;
            _detach(new common_ptr_type(r, dx));
        }

        explicit operator bool() const { return alive(); }
        explicit operator _Ty*() const { return get(); }

        template <class _Ty2>
        PROXY_PTR_NO_DISCARD bool operator==(
            const proxy::proxy_ptr<_Ty2>& _Right) const noexcept {
            return get() == _Right.get();
        }

        template <class _Ty2>
        PROXY_PTR_NO_DISCARD bool operator!=(
            const proxy::proxy_ptr<_Ty2>& _Right) const noexcept {
            return !(*this == _Right);
        }

        template <class _Ty2>
        PROXY_PTR_NO_DISCARD bool operator<(
            const proxy::proxy_ptr<_Ty2>& _Right) const noexcept {
            return get() < _Right.get();
        }

        template <class _Ty2>
        PROXY_PTR_NO_DISCARD bool operator>=(
            const proxy::proxy_ptr<_Ty2>& _Right) const noexcept {
            return !(*this < _Right);
        }

        template <class _Ty2>
        PROXY_PTR_NO_DISCARD bool operator>(
            const proxy::proxy_ptr<_Ty2>& _Right) const noexcept {
            return _Right < *this;
        }

        template <class _Ty2>
        PROXY_PTR_NO_DISCARD bool operator<=(
            const proxy::proxy_ptr<_Ty2>& _Right) const noexcept {
            return !(_Right < *this);
        }

        _Ty* get() const {
            if (!_is_Pointing())
                return nullptr;
            return _ppobj->get();
        }

        _Ty* ptr() const { return get(); }

        template <class _Ty2 = _Ty,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(_Ty2)>>
        _Ty2* operator->() const {
            assert(_is_Pointing());
            return get();
        }

        template <class _Ty2 = _Ty,
                  class = std::enable_if_t<PROXY_PTR_IS_ARRAY(_Ty2)>>
        _Ty2& operator[](std::ptrdiff_t p) const {
            assert(_is_Pointing());
            return (*get())[p];
        }

        template <class _Ty2 = _Ty,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(_Ty2)>>
        _Ty2& operator*() const {
            assert(_is_Pointing());
            return *get();
        }

        decltype(auto) operator=(const proxy_ptr<_Ty>& r) {
            _detach(r._ppobj);
            return (*this);
        }

        decltype(auto) operator=(std::nullptr_t) {
            _detach();
            return (*this);
        }

        _Ty* proxy_release() {
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
        void _detach(_common_Ptr_Ty* n = nullptr) {
            if (_ppobj)
                if (!_ppobj->dec_ref())
                    delete (_ppobj);

            _ppobj = n;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       private:
        _common_Ptr_Ty* _ppobj = nullptr;
    };

    template <class _Ty> class proxy_parent_base {
       public:
        proxy_ptr<_Ty> proxy() { return {_proxyPtr}; }
        void proxy_delete() {
            auto ret = _proxyPtr.proxy_release();
            _proxyPtr = static_cast<_Ty*>(this);
        }
        virtual ~proxy_parent_base() {
            auto ret = _proxyPtr.proxy_release();
            PROXY_PTR_UNUSED(ret);
        }

       private:
        proxy_ptr<_Ty> _proxyPtr{static_cast<_Ty*>(this)};
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

}  // namespace proxy

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator==(const proxy::proxy_ptr<_Ty>& _Left,
                                     std::nullptr_t) noexcept {
    return !_Left;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator==(
    std::nullptr_t, const proxy::proxy_ptr<_Ty>& _Right) noexcept {
    return !_Right;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator!=(const proxy::proxy_ptr<_Ty>& _Left,
                                     std::nullptr_t _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator!=(
    std::nullptr_t _Left, const proxy::proxy_ptr<_Ty>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<(const proxy::proxy_ptr<_Ty>& _Left,
                                    std::nullptr_t _Right) noexcept {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<(
    std::nullptr_t _Left, const proxy::proxy_ptr<_Ty>& _Right) noexcept {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>=(const proxy::proxy_ptr<_Ty>& _Left,
                                     std::nullptr_t _Right) noexcept {
    return !(_Left < _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>=(std::nullptr_t _Left,
                                     const proxy::proxy_ptr<_Ty>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>(const proxy::proxy_ptr<_Ty>& _Left,
                                    std::nullptr_t _Right) {
    return _Right < _Left;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>(std::nullptr_t _Left,
                                    const proxy::proxy_ptr<_Ty>& _Right) {
    return _Right < _Left;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<=(const proxy::proxy_ptr<_Ty>& _Left,
                                     std::nullptr_t _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<=(std::nullptr_t _Left,
                                     const proxy::proxy_ptr<_Ty>& _Right) {
    return !(_Right < _Left);
}

//

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator==(const proxy::proxy_ptr<_Ty>& _Left,
                                     const _Ty* const _ptr) noexcept {
    return _Left.get() == _ptr;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator==(
    const _Ty* _ptr, const proxy::proxy_ptr<_Ty>& _Right) noexcept {
    return _Right.get() == _ptr;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator!=(const proxy::proxy_ptr<_Ty>& _Left,
                                     const _Ty* const _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator!=(
    const _Ty* _Left, const proxy::proxy_ptr<_Ty>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<(const proxy::proxy_ptr<_Ty>& _Left,
                                    const _Ty* const _Right) {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<(const _Ty* const _Left,
                                    const proxy::proxy_ptr<_Ty>& _Right) {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>=(const proxy::proxy_ptr<_Ty>& _Left,
                                     const _Ty* const _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>=(const _Ty* const _Left,
                                     const proxy::proxy_ptr<_Ty>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>(const proxy::proxy_ptr<_Ty>& _Left,
                                    const _Ty* const _Right) {
    return _Right < _Left;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>(const _Ty* const _Left,
                                    const proxy::proxy_ptr<_Ty>& _Right) {
    return _Right < _Left;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<=(const proxy::proxy_ptr<_Ty>& _Left,
                                     const _Ty* const _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<=(const _Ty* const _Left,
                                     const proxy::proxy_ptr<_Ty>& _Right) {
    return !(_Right < _Left);
}

//

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator==(const proxy::proxy_ptr<_Ty>& _Left,
                                     _Ty* const _ptr) noexcept {
    return _Left.get() == _ptr;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator==(
    _Ty* const _ptr, const proxy::proxy_ptr<_Ty>& _Right) noexcept {
    return _Right.get() == _ptr;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator!=(const proxy::proxy_ptr<_Ty>& _Left,
                                     _Ty* const _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator!=(
    _Ty* const _Left, const proxy::proxy_ptr<_Ty>& _Right) noexcept {
    return !(_Left == _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<(const proxy::proxy_ptr<_Ty>& _Left,
                                    _Ty* const _Right) {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<(_Ty* const _Left,
                                    const proxy::proxy_ptr<_Ty>& _Right) {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>=(const proxy::proxy_ptr<_Ty>& _Left,
                                     _Ty* const _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>=(_Ty* const _Left,
                                     const proxy::proxy_ptr<_Ty>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>(const proxy::proxy_ptr<_Ty>& _Left,
                                    _Ty* const _Right) {
    return _Right < _Left;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>(_Ty* const _Left,
                                    const proxy::proxy_ptr<_Ty>& _Right) {
    return _Right < _Left;
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<=(const proxy::proxy_ptr<_Ty>& _Left,
                                     _Ty* const _Right) {
    return !(_Right < _Left);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<=(_Ty* const _Left,
                                     const proxy::proxy_ptr<_Ty>& _Right) {
    return !(_Right < _Left);
}

template <class Type> struct std::hash<proxy::proxy_ptr<Type>> {
    size_t operator()(const proxy::proxy_ptr<Type> _ptr) const {
        return reinterpret_cast<std::uintptr_t>(_ptr.get());
    }
};

template <class Type> struct std::less<proxy::proxy_ptr<Type>> {
    bool operator()(const proxy::proxy_ptr<Type>& lhs,
                    const proxy::proxy_ptr<Type>& rhs) const {
        return lhs < rhs;
    }
};

#endif
