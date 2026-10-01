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
    #include <cstdint>
    #include <memory>
    #include <utility>

    #define PROXY_PTR_NO_DISCARD [[nodiscard]]
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
    template <typename Ty> using enable_proxy_from_this = proxy_parent_base<Ty>;

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

        template <class Ty> struct _deduce_alive_type;
        template <> struct _deduce_alive_type<proxy_atomic> {
            using type = std::atomic<bool>;
        };
        template <> struct _deduce_alive_type<proxy_non_atomic> {
            using type = bool;
        };

        template <class Ty>
        using deduce_alive_type = typename _deduce_alive_type<Ty>::type;

        template <class Ty>
        using deduce_ref_count_type = typename _deduce_ref_count_type<Ty>::type;

        template <class AtomicType> class _proxy_common_state_base {
           protected:
            using ref_count_t = deduce_ref_count_type<AtomicType>;
            using alive_t = deduce_alive_type<AtomicType>;
            void* _ptr = nullptr;
            ref_count_t _ref_count = static_cast<size_t>(0);
            alive_t _alive = false;

           public:
            _proxy_common_state_base(void* p) : _ptr(p) { _alive = true; }

            void inc_ref() {
                if constexpr (std::is_same_v<AtomicType, proxy_atomic>) {
                    _ref_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    ++_ref_count;
                }
            }
            bool dec_ref() {
                if (_ref_count == 0)
                    return false;
                if constexpr (std::is_same_v<AtomicType, proxy_atomic>) {
                    return _ref_count.fetch_sub(1, std::memory_order_acq_rel) != 1;
                } else {
                    return --_ref_count != 0;
                }
            }

            bool alive() const { return _alive; }
            bool expired() const { return !alive(); }
            void* get() const { return _ptr; }
            void* release() {
                _alive = false;
                return _ptr;
            }

            virtual bool is_weak() const = 0;
            virtual void delete_ptr() = 0;
            virtual ~_proxy_common_state_base() {}
        };

        template <class Type> struct non_deleter {
            void operator()(Type*) noexcept {}
        };

        // Empty, non-final deleters stay a base so EBO keeps the state small;
        // anything else (function pointers, final or stateful classes) is a member.
        template <class Dex, bool = std::is_empty_v<Dex> && !std::is_final_v<Dex>>
        class _deleter_holder : private Dex {
           protected:
            _deleter_holder() = default;
            explicit _deleter_holder(Dex dx) : Dex(std::move(dx)) {}
            Dex& _deleter() { return *this; }
        };

        template <class Dex> class _deleter_holder<Dex, false> {
           protected:
            _deleter_holder() = default;
            explicit _deleter_holder(Dex dx) : _dx(std::move(dx)) {}
            Dex& _deleter() { return _dx; }

           private:
            Dex _dx{};
        };

        template <class Type, class Dex, class AtomicType>
        class _proxy_common_state
            : private _deleter_holder<Dex>,
              public _proxy_common_state_base<AtomicType> {
           public:
            _proxy_common_state(Type* ptr)
                : _proxy_common_state_base<AtomicType>(ptr) {}
            _proxy_common_state(Type* ptr, Dex dx)
                : _deleter_holder<Dex>(std::move(dx)),
                  _proxy_common_state_base<AtomicType>(ptr) {}

            bool is_weak() const override {
                using WeakDeleter = detail::non_deleter<Type>;
                return std::is_same_v<Dex, WeakDeleter>;
            }
            void delete_ptr() override {
                if (this->_ptr && this->_alive) {
                    this->_deleter()(static_cast<Type*>(this->_ptr));
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

        template <class Type1, class Type2>
        constexpr bool is_proxy_valid_cast =
            std::is_base_of_v<Type1, Type2> ||
            std::is_base_of_v<Type2, Type1> ||
            std::is_same_v<Type1, std::remove_const_t<Type2>> ||
            std::is_same_v<Type2, std::remove_const_t<Type1>>;

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

        template <class To, class From, class = void>
        struct _is_static_castable : std::false_type {};
        template <class To, class From>
        struct _is_static_castable<
            To, From,
            std::void_t<decltype(static_cast<To*>(std::declval<From*>()))>>
            : std::true_type {};

        // Castable both ways only when no virtual base lies between the types:
        // the cast is then a pure address offset that never reads the object,
        // so it stays meaningful after the object was deleted.
        template <class To, class From>
        constexpr bool is_offset_cast = _is_static_castable<To, From>::value &&
                                        _is_static_castable<From, To>::value;

        // proxy_ptr / proxy_owner_ptr (specialized once both are declared)
        template <class Ty> struct _is_proxy_handle : std::false_type {};
        template <class Ty>
        constexpr bool is_proxy_handle = _is_proxy_handle<Ty>::value;

        template <class H>
        using enable_if_handle = std::enable_if_t<is_proxy_handle<H>, int>;
        template <class L, class R>
        using enable_if_handles =
            std::enable_if_t<is_proxy_handle<L> && is_proxy_handle<R>, int>;

        // Derived -> Base (or T -> const T) without an explicit cast, like shared_ptr.
        template <class From, class To>
        constexpr bool is_proxy_upcast =
            !PROXY_PTR_IS_ARRAY(From) && !PROXY_PTR_IS_ARRAY(To) &&
            !std::is_same_v<From, To> && std::is_convertible_v<From*, To*>;
    }  // namespace detail

    template <class _RTy, class AtomicTypeFlag = proxy_non_atomic,
              class = detail::enable_valid_atomic_flag<AtomicTypeFlag>>
    class proxy_owner_ptr;

    template <class _RTy, class AtomicTypeFlag = proxy_non_atomic,
              class = detail::enable_valid_atomic_flag<AtomicTypeFlag>>
    class proxy_ptr {
       public:
        using Type = detail::extract_proxy_type<_RTy>;
        using _common_PtrType =
            detail::_proxy_common_state_base<AtomicTypeFlag>;

        detail::_proxy_common_state_base<AtomicTypeFlag>* _state() const {
            return _ppobj;
        }

        proxy_ptr() {}
        proxy_ptr(std::nullptr_t) {}
        proxy_ptr(const proxy_ptr& n) { _proxy_from(n); }
        proxy_ptr(proxy_ptr&& other) noexcept
            : _ppobj(other._ppobj), _ptr(other._ptr) {
            other._ppobj = nullptr;
            other._ptr = nullptr;
        }
        template <
            class Type2,
            std::enable_if_t<detail::is_proxy_valid_cast<Type, Type2>, int> = 0>
        explicit proxy_ptr(Type* ptr,
                           const proxy_ptr<Type2, AtomicTypeFlag>& other) {
            _detach(other._state(), ptr);
        }

        // Implicit up-casts. Pure-offset conversions keep an expired source's
        // identity (hashkey); a virtual-base conversion would read the deleted
        // object, so it only uses the live pointer.
        template <class Y,
                  std::enable_if_t<detail::is_proxy_upcast<Y, _RTy>, int> = 0>
        proxy_ptr(const proxy_ptr<Y, AtomicTypeFlag>& other) {
            if constexpr (detail::is_offset_cast<std::remove_cv_t<Type>,
                                                 std::remove_cv_t<Y>>)
                _detach(other._state(), other.hashkey());
            else
                _detach(other._state(), other.get());
        }

        template <class Y,
                  std::enable_if_t<detail::is_proxy_upcast<Y, _RTy>, int> = 0>
        proxy_ptr(const proxy_owner_ptr<Y, AtomicTypeFlag>& other)
            : proxy_ptr(static_cast<const proxy_ptr<Y, AtomicTypeFlag>&>(other)) {}

        explicit operator bool() const { return alive(); }
        explicit operator Type*() const { return get(); }

        PROXY_PTR_NO_DISCARD Type* hashkey() const {
            if (!_is_Pointing())
                return nullptr;
            return _ptr;
        }

        PROXY_PTR_NO_DISCARD Type* get() const {
            if (!_is_Pointing() || !_ppobj->alive())
                return nullptr;
            return _ptr;
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(Type2)>>
        PROXY_PTR_NO_DISCARD Type2* operator->() const {
            assert(_is_Pointing() && alive());
            return get();
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<PROXY_PTR_IS_ARRAY(Type2)>>
        PROXY_PTR_NO_DISCARD Type2& operator[](std::ptrdiff_t p) const {
            assert(_is_Pointing() && alive());
            return (*get())[p];
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(Type2)>>
        PROXY_PTR_NO_DISCARD Type2& operator*() const {
            assert(_is_Pointing() && alive());
            return *get();
        }

        decltype(auto) operator=(const proxy_ptr<Type, AtomicTypeFlag>& r) {
            _proxy_from(r);
            return (*this);
        }

        proxy_ptr& operator=(proxy_ptr&& other) noexcept {
            if (this != &other) {
                if (_ppobj && !_ppobj->dec_ref())
                    delete _ppobj;
                _ppobj = other._ppobj;
                _ptr = other._ptr;
                other._ppobj = nullptr;
                other._ptr = nullptr;
            }
            return *this;
        }

        decltype(auto) operator=(std::nullptr_t) {
            _detach();
            return (*this);
        }

        PROXY_PTR_NO_DISCARD bool alive() const {
            return _is_Pointing() && _ppobj->alive() && _ptr;
        }

        PROXY_PTR_NO_DISCARD bool expired() const { return !alive(); }

        PROXY_PTR_NO_DISCARD bool _is_weakref() const { return _ppobj && _ppobj->is_weak(); }

        ~proxy_ptr() { _detach(); }

       protected:
        void _proxy_from(const proxy_ptr& n) {
            if (n._ppobj == _ppobj) {
                _ptr = n._ptr;
                return;
            }
            _detach(n._ppobj, n._ptr);
        }
        bool _is_Pointing() const { return _ppobj != nullptr; }
        void _detach(_common_PtrType* n = nullptr, Type* p = nullptr) {
            if (_ppobj)
                if (!_ppobj->dec_ref())
                    delete (_ppobj);

            _ppobj = n;
            _ptr = p;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       private:
        // Owning constructors: reachable only through proxy_owner_ptr (and so
        // make_proxy). A public one would let direct-init (emplace(this),
        // pair conversions) build a second owning block behind the real owner.
        template <class, class, class> friend class proxy_owner_ptr;

        explicit proxy_ptr(Type* r) {
            using deleter_type = std::default_delete<_RTy>;
            using common_ptr_type =
                detail::_proxy_common_state<Type, deleter_type, AtomicTypeFlag>;
            _detach(new common_ptr_type(r), r);
        }
        template <class Dex, std::enable_if_t<
                                 detail::is_valid_deleter<Type, Dex>, int> = 0>
        explicit proxy_ptr(Type* r, Dex dx) {
            using common_ptr_type =
                detail::_proxy_common_state<Type, Dex, AtomicTypeFlag>;
            _detach(new common_ptr_type(r, std::move(dx)), r);
        }

        _common_PtrType* _ppobj = nullptr;
        Type* _ptr = nullptr;
    };

    // Move-only owning proxy: the only type that exposes proxy_delete().
    // Holds its observer by composition, not inheritance: an owner must never
    // bind to proxy_ptr& (assigning through that reference would retarget the
    // owner) and moving it into a proxy_ptr must not strip its ownership.
    // Converts implicitly to a proxy_ptr<T> observer copy.
    template <class _RTy, class AtomicTypeFlag, class>
    class proxy_owner_ptr {
        using observer = proxy_ptr<_RTy, AtomicTypeFlag>;

       public:
        using Type = typename observer::Type;

        proxy_owner_ptr() = default;
        proxy_owner_ptr(std::nullptr_t) {}

        // Move-only: no copy
        proxy_owner_ptr(const proxy_owner_ptr&) = delete;
        proxy_owner_ptr& operator=(const proxy_owner_ptr&) = delete;
        proxy_owner_ptr(proxy_owner_ptr&&) noexcept = default;
        proxy_owner_ptr& operator=(proxy_owner_ptr&&) noexcept = default;

        proxy_owner_ptr& operator=(std::nullptr_t) {
            _obs = nullptr;
            return *this;
        }

        // Owning constructors (raw pointer with default or custom deleter)
        explicit proxy_owner_ptr(Type* r) : _obs(r) {}

        template <class Dex,
                  std::enable_if_t<detail::is_valid_deleter<Type, Dex>, int> = 0>
        explicit proxy_owner_ptr(Type* r, Dex dx) : _obs(r, std::move(dx)) {}

        operator const observer&() const noexcept { return _obs; }

        explicit operator bool() const { return alive(); }
        explicit operator Type*() const { return get(); }

        PROXY_PTR_NO_DISCARD Type* hashkey() const { return _obs.hashkey(); }
        PROXY_PTR_NO_DISCARD Type* get() const { return _obs.get(); }
        PROXY_PTR_NO_DISCARD bool alive() const { return _obs.alive(); }
        PROXY_PTR_NO_DISCARD bool expired() const { return _obs.expired(); }
        PROXY_PTR_NO_DISCARD bool _is_weakref() const { return _obs._is_weakref(); }
        detail::_proxy_common_state_base<AtomicTypeFlag>* _state() const {
            return _obs._state();
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(Type2)>>
        PROXY_PTR_NO_DISCARD Type2* operator->() const {
            return _obs.operator->();
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<PROXY_PTR_IS_ARRAY(Type2)>>
        PROXY_PTR_NO_DISCARD Type2& operator[](std::ptrdiff_t p) const {
            return _obs[p];
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<!PROXY_PTR_IS_ARRAY(Type2)>>
        PROXY_PTR_NO_DISCARD Type2& operator*() const {
            return *_obs;
        }

        // The ONLY place proxy_delete() exists in the entire system
        void proxy_delete() {
            if (_state())
                _state()->delete_ptr();
        }

        PROXY_PTR_NO_DISCARD Type* proxy_release() {
            if (!_state())
                return nullptr;
            return static_cast<Type*>(_state()->release());
        }

       private:
        observer _obs;
    };

    namespace detail {
        template <class T, class A, class E>
        struct _is_proxy_handle<proxy_ptr<T, A, E>> : std::true_type {};
        template <class T, class A, class E>
        struct _is_proxy_handle<proxy_owner_ptr<T, A, E>> : std::true_type {};
    }  // namespace detail

    template <class T, class U, class A = proxy_non_atomic>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> static_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept;

    template <class T, class U, class A = proxy_non_atomic>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> dynamic_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept;

    template <class T, class U, class A = proxy_non_atomic>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> const_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept;

    template <class T, class U, class A = proxy_non_atomic>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> reinterpret_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept;

    // Casting an owner yields an observer, same as casting its proxy_ptr view.
    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> static_pointer_cast(
        const proxy::proxy_owner_ptr<U, A>& r) noexcept {
        return proxy::static_pointer_cast<T>(
            static_cast<const proxy::proxy_ptr<U, A>&>(r));
    }

    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> dynamic_pointer_cast(
        const proxy::proxy_owner_ptr<U, A>& r) noexcept {
        return proxy::dynamic_pointer_cast<T>(
            static_cast<const proxy::proxy_ptr<U, A>&>(r));
    }

    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> const_pointer_cast(
        const proxy::proxy_owner_ptr<U, A>& r) noexcept {
        return proxy::const_pointer_cast<T>(
            static_cast<const proxy::proxy_ptr<U, A>&>(r));
    }

    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> reinterpret_pointer_cast(
        const proxy::proxy_owner_ptr<U, A>& r) noexcept {
        return proxy::reinterpret_pointer_cast<T>(
            static_cast<const proxy::proxy_ptr<U, A>&>(r));
    }

    template <class Type> class proxy_parent_base {
       public:
        PROXY_PTR_NO_DISCARD proxy_ptr<Type> proxy() { return _proxyPtr; }
        PROXY_PTR_NO_DISCARD proxy_ptr<Type> proxy_from_this() { return _proxyPtr; }
        template <class Derived> PROXY_PTR_NO_DISCARD proxy_ptr<Derived> proxy_from_base() {
            return {proxy::static_pointer_cast<Derived>(_proxyPtr)};
        }
        void proxy_delete() {
            _proxyPtr.proxy_delete();  // non_deleter: no-op on memory, sets _alive=false
        }
        virtual ~proxy_parent_base() { _proxyPtr.proxy_delete(); }

       private:
        proxy_owner_ptr<Type> _proxyPtr{static_cast<Type*>(this),
                                        detail::non_deleter<Type>()};
    };

    namespace detail {
        template <class Ty, class Atomic> struct make_proxy {
            template <class... args>
            static proxy_owner_ptr<Ty, Atomic> construct(args&&... va) {
                return proxy_owner_ptr<Ty, Atomic>{new Ty(std::forward<args>(va)...)};
            }
        };

        template <class Ty, class Atomic> struct make_proxy<Ty[], Atomic> {
            static proxy_owner_ptr<Ty[], Atomic> construct(size_t len) {
                return proxy_owner_ptr<Ty[], Atomic>{new Ty[len]};
            }
        };
    }  // namespace detail

    template <class Ty, class... Args>
    PROXY_PTR_NO_DISCARD std::enable_if_t<detail::is_proxy_valid_type<Ty>, proxy_owner_ptr<Ty>>
    make_proxy(Args&&... Arguments) {
        return detail::make_proxy<Ty, proxy_non_atomic>::construct(
            std::forward<Args>(Arguments)...);
    }

    template <class Ty, class... Args>
    PROXY_PTR_NO_DISCARD std::enable_if_t<detail::is_proxy_valid_type<Ty>,
                     proxy_owner_ptr<Ty, proxy_atomic>>
    make_proxy_atomic(Args&&... Arguments) {
        return detail::make_proxy<Ty, proxy_atomic>::construct(
            std::forward<Args>(Arguments)...);
    }

    template <class Type, class AtomicType> struct proxy_factory {
        template <class... args>
        PROXY_PTR_NO_DISCARD static proxy::proxy_owner_ptr<Type, AtomicType> make(
            args&&... arg) {
            return detail::make_proxy<Type, AtomicType>::construct(
                std::forward<args>(arg)...);
        }
    };

    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> static_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept {
        using To = typename proxy::proxy_ptr<T, A>::Type;
        using From = typename proxy::proxy_ptr<U, A>::Type;
        // Expired sources keep their identity (hashkey) when the cast is a pure
        // offset; a virtual-base cast would read the deleted object, so it
        // only uses the live pointer.
        To* p;
        if constexpr (detail::is_offset_cast<To, From>)
            p = static_cast<To*>(r.hashkey());
        else
            p = static_cast<To*>(r.get());
        return proxy::proxy_ptr<T, A>{p, r};
    }

    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> dynamic_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept {
        if (auto p = dynamic_cast<typename proxy::proxy_ptr<T, A>::Type*>(r.get()))
            return proxy::proxy_ptr<T, A>{p, r};
        else
            return proxy::proxy_ptr<T, A>{};
    }

    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> const_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept {
        auto p = const_cast<typename proxy::proxy_ptr<T, A>::Type*>(r.hashkey());
        return proxy::proxy_ptr<T, A>{p, r};
    }

    template <class T, class U, class A>
    PROXY_PTR_NO_DISCARD proxy::proxy_ptr<T, A> reinterpret_pointer_cast(
        const proxy::proxy_ptr<U, A>& r) noexcept {
        auto p = reinterpret_cast<typename proxy::proxy_ptr<T, A>::Type*>(r.hashkey());
        return proxy::proxy_ptr<T, A>{p, r};
    }

    // ── Comparisons ─────────────────────────────────────────────────────────
    // In namespace proxy so ADL finds them for proxy_ptr and proxy_owner_ptr
    // alike. Identity is hashkey(); `== nullptr` means "not alive".

    template <class L, class R, detail::enable_if_handles<L, R> = 0>
    PROXY_PTR_NO_DISCARD bool operator==(const L& _Left, const R& _Right) noexcept {
        return _Left.hashkey() == _Right.hashkey();
    }
    template <class L, class R, detail::enable_if_handles<L, R> = 0>
    PROXY_PTR_NO_DISCARD bool operator!=(const L& _Left, const R& _Right) noexcept {
        return !(_Left == _Right);
    }
    template <class L, class R, detail::enable_if_handles<L, R> = 0>
    PROXY_PTR_NO_DISCARD bool operator<(const L& _Left, const R& _Right) noexcept {
        return _Left.hashkey() < _Right.hashkey();
    }
    template <class L, class R, detail::enable_if_handles<L, R> = 0>
    PROXY_PTR_NO_DISCARD bool operator>=(const L& _Left, const R& _Right) noexcept {
        return !(_Left < _Right);
    }
    template <class L, class R, detail::enable_if_handles<L, R> = 0>
    PROXY_PTR_NO_DISCARD bool operator>(const L& _Left, const R& _Right) noexcept {
        return _Right < _Left;
    }
    template <class L, class R, detail::enable_if_handles<L, R> = 0>
    PROXY_PTR_NO_DISCARD bool operator<=(const L& _Left, const R& _Right) noexcept {
        return !(_Right < _Left);
    }

    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator==(const H& _Left, std::nullptr_t) noexcept {
        return !_Left;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator==(std::nullptr_t, const H& _Right) noexcept {
        return !_Right;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator!=(const H& _Left, std::nullptr_t _Right) noexcept {
        return !(_Left == _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator!=(std::nullptr_t _Left, const H& _Right) noexcept {
        return !(_Left == _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<(const H& _Left, std::nullptr_t _Right) noexcept {
        return std::less<typename H::Type*>()(_Left.hashkey(), _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<(std::nullptr_t _Left, const H& _Right) noexcept {
        return std::less<typename H::Type*>()(_Left, _Right.hashkey());
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>=(const H& _Left, std::nullptr_t _Right) noexcept {
        return !(_Left < _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>=(std::nullptr_t _Left, const H& _Right) noexcept {
        return !(_Left < _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>(const H& _Left, std::nullptr_t _Right) noexcept {
        return _Right < _Left;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>(std::nullptr_t _Left, const H& _Right) noexcept {
        return _Right < _Left;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<=(const H& _Left, std::nullptr_t _Right) noexcept {
        return !(_Right < _Left);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<=(std::nullptr_t _Left, const H& _Right) noexcept {
        return !(_Right < _Left);
    }

    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator==(
        const H& _Left, const typename H::Type* _Right) noexcept {
        return _Left.hashkey() == _Right;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator==(
        const typename H::Type* _Left, const H& _Right) noexcept {
        return _Right.hashkey() == _Left;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator!=(
        const H& _Left, const typename H::Type* _Right) noexcept {
        return !(_Left == _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator!=(
        const typename H::Type* _Left, const H& _Right) noexcept {
        return !(_Left == _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<(
        const H& _Left, const typename H::Type* _Right) noexcept {
        return std::less<const typename H::Type*>()(_Left.hashkey(), _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<(
        const typename H::Type* _Left, const H& _Right) noexcept {
        return std::less<const typename H::Type*>()(_Left, _Right.hashkey());
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>=(
        const H& _Left, const typename H::Type* _Right) noexcept {
        return !(_Left < _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>=(
        const typename H::Type* _Left, const H& _Right) noexcept {
        return !(_Left < _Right);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>(
        const H& _Left, const typename H::Type* _Right) noexcept {
        return _Right < _Left;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator>(
        const typename H::Type* _Left, const H& _Right) noexcept {
        return _Right < _Left;
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<=(
        const H& _Left, const typename H::Type* _Right) noexcept {
        return !(_Right < _Left);
    }
    template <class H, detail::enable_if_handle<H> = 0>
    PROXY_PTR_NO_DISCARD bool operator<=(
        const typename H::Type* _Left, const H& _Right) noexcept {
        return !(_Right < _Left);
    }

}  // namespace proxy

template <class Type, class AtomicType>
struct std::hash<proxy::proxy_ptr<Type, AtomicType>> {
    size_t operator()(const proxy::proxy_ptr<Type, AtomicType>& _ptr) const {
        return reinterpret_cast<std::uintptr_t>(_ptr.hashkey());
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
