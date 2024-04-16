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
#ifndef INCLUDE_OWNER_OWNER_H
    #define INCLUDE_OWNER_OWNER_H

// #define PROXY_THREAD_SAFE

    #include <type_traits>
    #include <assert.h>
    #include <atomic>
    #include <memory>
    #ifdef PROXY_THREAD_SAFE
        #include <shared_mutex>
        #include <optional>
    #endif

    #define OWNER_PTR_NO_DISCARD [[nodiscard]]
    #define OWNER_PTR_UNUSED(v) ((void)v)
    #define OWNER_PTR_IS_ARRAY(type) std::is_array_v<type>
    #define OWNER_PTR_EXTENT(type) std::extent_v<type>
    #define OWNER_PTR_CONSTEXPR(expr) constexpr(expr)

namespace owner {
    // atomic types
    struct owner_atomic {};
    struct owner_non_atomic {};

    // ownership linking
    struct ownership_link {};
    struct ownership_no_link {};

    #ifdef PROXY_THREAD_SAFE
    // helper alias shortcut
    using owner_lock = std::unique_lock<std::shared_mutex>;
    using owner_shared_lock = std::shared_lock<std::shared_mutex>;
    #endif

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
        template <> struct _deduce_ref_count_type<owner_atomic> {
            using type = std::atomic<size_t>;
        };
        template <> struct _deduce_ref_count_type<owner_non_atomic> {
            using type = size_t;
        };

        template <class Ty>
        using deduce_ref_count_type = typename _deduce_ref_count_type<Ty>::type;

        template <class AtomicType> class _owner_common_state_base {
           protected:
            using ref_count_t = deduce_ref_count_type<AtomicType>;
            void* _ptr = nullptr;
            ref_count_t _ref_count = static_cast<std::size_t>(0);
            ref_count_t _weak_ref_count = static_cast<std::size_t>(0);
            bool _alive = false;
    #ifdef PROXY_THREAD_SAFE
            std::shared_mutex _mtx;
    #endif

           public:
            _owner_common_state_base(void* p) : _ptr(p) {
                _alive = p != nullptr;
            }

            void inc_ref() { _ref_count++; }

            void dec_ref() {
                if (_ref_count > 0)
                    _ref_count--;
            }

            void inc_weak_ref() { _weak_ref_count++; }

            void dec_weak_ref() {
                if (_weak_ref_count > 0)
                    _weak_ref_count--;
            }

            std::size_t ref_count() const {
                return static_cast<std::size_t>(_ref_count);
            }

            std::size_t weak_ref_count() const {
                return static_cast<std::size_t>(_weak_ref_count);
            }

            bool has_ref() const {
                return _weak_ref_count != 0 || _ref_count != 0;
            }

            bool alive() const { return _alive; }
            bool expired() const { return !alive(); }
            void* get() const { return _ptr; }
            void* release() {
                _alive = false;
                return _ptr;
            }

    #ifdef PROXY_THREAD_SAFE
            owner_shared_lock shared_lock() { return owner_shared_lock{_mtx}; }
            owner_lock lock() { return owner_lock{_mtx}; }
    #endif

            virtual void delete_ptr() = 0;
            virtual ~_owner_common_state_base() {}
        };

        template <class Type> struct non_deleter {
            void operator()(Type* ptr) noexcept {}
        };

        template <class Type, class Dex, class AtomicType>
        class _owner_common_state
            : private Dex,
              public _owner_common_state_base<AtomicType> {
           public:
            _owner_common_state(Type* ptr)
                : _owner_common_state_base<AtomicType>(ptr) {}
            _owner_common_state(Type* ptr, const Dex& dx)
                : _owner_common_state_base<AtomicType>(ptr) {
                static_cast<Dex&>(*this) = dx;
            }

            void delete_ptr() override {
                if (this->_ptr && this->_alive) {
                    static_cast<Dex&>(*this)(static_cast<Type*>(this->_ptr));
                    this->_alive = false;
                }
            }
            virtual ~_owner_common_state() { delete_ptr(); }
        };

        template <class Ty> struct _extract_owner_pointer_type {
            using type = Ty*;
        };
        template <class Ty> struct _extract_owner_pointer_type<Ty[]> {
            using type = Ty*;
        };

        template <class Ty>
        using extract_owner_pointer_type =
            typename _extract_owner_pointer_type<Ty>::type;

        template <class Ty>
        using extract_owner_type =
            std::remove_pointer_t<extract_owner_pointer_type<Ty>>;

        template <class Type1, class Type2>
        constexpr bool is_owner_valid_cast =
            std::is_base_of_v<Type1, Type2> ||
            std::is_base_of_v<Type2, Type1> ||
            std::is_same_v<Type1, std::remove_const_t<Type2>> ||
            std::is_same_v<Type2, std::remove_const_t<Type1>>;

        template <class Ty>
        constexpr bool is_owner_valid_type =
            !OWNER_PTR_IS_ARRAY(Ty) ||
            (OWNER_PTR_IS_ARRAY(Ty) && OWNER_PTR_EXTENT(Ty) == 0);

        template <class Type, class Dex>
        constexpr bool is_valid_deleter =
            std::is_move_constructible_v<Dex> &&
            detail::_can_call_function_object<Dex&, Type*&>::value;

        template <class Ty>
        constexpr bool is_valid_atomic_flag =
            std::is_same_v<Ty, owner_atomic> ||
            std::is_same_v<Ty, owner_non_atomic>;
    }  // namespace detail

    template <class T>
    concept valid_atomic_flag = detail::is_valid_atomic_flag<T>;

    template <class _RTy, valid_atomic_flag AtomicTypeFlag = owner_non_atomic>
    class owner_ptr {
       public:
        using Type = detail::extract_owner_type<_RTy>;

        detail::_owner_common_state_base<AtomicTypeFlag>* _state() const {
            return _ppobj;
        }

       private:
        using _common_PtrType =
            detail::_owner_common_state_base<AtomicTypeFlag>;

       protected:
        owner_ptr(_common_PtrType* _ptr) {
            _ppobj = _ptr;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       public:
        owner_ptr() {}
        owner_ptr(std::nullptr_t) {}
        owner_ptr(const owner_ptr& n) { _owner_from(n); }
        explicit owner_ptr(Type* r) {
            using deleter_type = std::default_delete<Type>;
            using common_ptr_type =
                detail::_owner_common_state<Type, deleter_type, AtomicTypeFlag>;
            _detach(new common_ptr_type(r));
        }
        template <class Dex, std::enable_if_t<
                                 detail::is_valid_deleter<Type, Dex>, int> = 0>
        explicit owner_ptr(Type* r, const Dex& dx) {
            using common_ptr_type =
                detail::_owner_common_state<Type, Dex, AtomicTypeFlag>;
            _detach(new common_ptr_type(r, dx));
        }

        template <
            class Type2,
            std::enable_if_t<detail::is_owner_valid_cast<Type, Type2>, int> = 0>
        explicit owner_ptr(Type* ptr, owner_ptr<Type2, AtomicTypeFlag>* other) {
            // assert(other._is_Pointing());
            OWNER_PTR_UNUSED(ptr);
            _detach(other->_state());
        }

        explicit owner_ptr(Type* ptr, _common_PtrType* state) {
            // assert(other._is_Pointing());
            OWNER_PTR_UNUSED(ptr);
            _detach(state);
        }

        explicit operator bool() const { return alive(); }
        explicit operator Type*() const { return get(); }

        template <class Type2, class AtomicType2>
        OWNER_PTR_NO_DISCARD bool operator==(
            const owner::owner_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return hashkey() == _Right.hashkey();
        }

        template <class Type2, class AtomicType2>
        OWNER_PTR_NO_DISCARD bool operator!=(
            const owner::owner_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return !(*this == _Right);
        }

        template <class Type2, class AtomicType2>
        OWNER_PTR_NO_DISCARD bool operator<(
            const owner::owner_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return hashkey() < _Right.hashkey();
        }

        template <class Type2, class AtomicType2>
        OWNER_PTR_NO_DISCARD bool operator>=(
            const owner::owner_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return !(*this < _Right);
        }

        template <class Type2, class AtomicType2>
        OWNER_PTR_NO_DISCARD bool operator>(
            const owner::owner_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return _Right < *this;
        }

        template <class Type2, class AtomicType2>
        OWNER_PTR_NO_DISCARD bool operator<=(
            const owner::owner_ptr<Type2, AtomicType2>& _Right) const noexcept {
            return !(_Right < *this);
        }

        Type* hashkey() const {
            if (!_is_Pointing())
                return nullptr;
            return static_cast<Type*>(_ppobj->get());
        }

        Type* get() const { return alive() ? hashkey() : nullptr; }

        template <class Type2 = Type,
                  class = std::enable_if_t<!OWNER_PTR_IS_ARRAY(Type2)>>
        Type2* operator->() const {
            assert(_is_Pointing() && alive());
            return get();
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<OWNER_PTR_IS_ARRAY(Type2)>>
        Type2& operator[](std::ptrdiff_t p) const {
            assert(_is_Pointing() && alive());
            return (*get())[p];
        }

        template <class Type2 = Type,
                  class = std::enable_if_t<!OWNER_PTR_IS_ARRAY(Type2)>>
        Type2& operator*() const {
            assert(_is_Pointing() && alive());
            return *get();
        }

        decltype(auto) operator=(const owner_ptr<Type, AtomicTypeFlag>& r) {
            _detach(r._ppobj);
            return (*this);
        }

        decltype(auto) operator=(std::nullptr_t) {
            _detach();
            return (*this);
        }

        Type* owner_release() {
            if (!_is_Pointing())
                return nullptr;
            return static_cast<Type*>(_ppobj->release());
        }

        void owner_delete() {
            if (_is_Pointing())
                _ppobj->delete_ptr();
        }

        bool alive() const {
            return _is_Pointing() && _ppobj->alive() && _ppobj->get();
        }

        bool expired() const { return !alive(); }

    #ifdef PROXY_THREAD_SAFE
        std::optional<owner_shared_lock> try_shared_lock() {
            if (!alive())
                return std::nullopt;
            auto lock = _ppobj->shared_lock();
            if (!alive())
                return std::nullopt;
            return lock;
        }

        std::optional<owner_lock> try_lock() {
            if (!alive())
                return std::nullopt;
            auto lock = _ppobj->lock();
            if (!alive())
                return std::nullopt;
            return lock;
        }
    #endif

        ~owner_ptr() { _detach(); }

       protected:
        void _owner_from(const owner_ptr& n) {
            if (n._ppobj == _ppobj)
                return;
            _detach(n._ppobj);
        }
        bool _is_Pointing() const { return _ppobj != nullptr; }
        void _detach(_common_PtrType* n = nullptr) {
            if (_ppobj) {
                // decreasing ownership ref count
                _ppobj->dec_ref();
                // checking if common state has no ref (including weakref)
                if (!_ppobj->has_ref())
                    delete (_ppobj);
                // checking if common state has only weakref
                else if (_ppobj->ref_count() == 0)
                    _ppobj->delete_ptr();
            }

            // updating pointed object state
            _ppobj = n;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       private:
        _common_PtrType* _ppobj = nullptr;
    };

    // forward declaration
    template <class Type> class proxy_parent_base;

    template <class T>
    concept valid_ownership = std::is_same_v<T, ownership_link> ||
                              std::is_same_v<T, ownership_no_link>;

    namespace detail {
        template <class _RTy, valid_atomic_flag AtomicTypeFlag,
                  valid_ownership OwnershipLink>
        class no_owner_ptr {
           public:
            using Type = detail::extract_owner_type<_RTy>;
            detail::_owner_common_state_base<AtomicTypeFlag>* _state() const {
                return _ppobj;
            }

           private:
            using _common_PtrType =
                detail::_owner_common_state_base<AtomicTypeFlag>;

           public:
            no_owner_ptr() {}
            no_owner_ptr(std::nullptr_t) {}
            no_owner_ptr(const no_owner_ptr& other) { _detach(other._state()); }
            no_owner_ptr(no_owner_ptr&& other) { _detach(other._state()); }

            explicit no_owner_ptr(Type* ptr, _common_PtrType* state) {
                OWNER_PTR_UNUSED(ptr);
                _detach(state);
            }

            template <class Type2,
                      std::enable_if_t<detail::is_owner_valid_cast<Type, Type2>,
                                       int> = 0>
            explicit no_owner_ptr(Type* ptr,
                                  const no_owner_ptr<Type2, AtomicTypeFlag,
                                                     OwnershipLink>& other) {
                // assert(other._is_Pointing());
                OWNER_PTR_UNUSED(ptr);
                _detach(other._state());
            }

            explicit operator bool() const { return alive(); }
            explicit operator Type*() const { return get(); }

            template <class Type2, class AtomicType2, class OwnershipLink2>
            OWNER_PTR_NO_DISCARD bool operator==(
                const no_owner_ptr<Type2, AtomicType2, OwnershipLink2>& _Right)
                const noexcept {
                return hashkey() == _Right.hashkey();
            }

            template <class Type2, class AtomicType2, class OwnershipLink2>
            OWNER_PTR_NO_DISCARD bool operator!=(
                const no_owner_ptr<Type2, AtomicType2, OwnershipLink2>& _Right)
                const noexcept {
                return !(*this == _Right);
            }

            template <class Type2, class AtomicType2, class OwnershipLink2>
            OWNER_PTR_NO_DISCARD bool operator<(
                const no_owner_ptr<Type2, AtomicType2, OwnershipLink2>& _Right)
                const noexcept {
                return hashkey() < _Right.hashkey();
            }

            template <class Type2, class AtomicType2, class OwnershipLink2>
            OWNER_PTR_NO_DISCARD bool operator>=(
                const no_owner_ptr<Type2, AtomicType2, OwnershipLink2>& _Right)
                const noexcept {
                return !(*this < _Right);
            }

            template <class Type2, class AtomicType2, class OwnershipLink2>
            OWNER_PTR_NO_DISCARD bool operator>(
                const no_owner_ptr<Type2, AtomicType2, OwnershipLink2>& _Right)
                const noexcept {
                return _Right < *this;
            }

            template <class Type2, class AtomicType2, class OwnershipLink2>
            OWNER_PTR_NO_DISCARD bool operator<=(
                const no_owner_ptr<Type2, AtomicType2, OwnershipLink2>& _Right)
                const noexcept {
                return !(_Right < *this);
            }

            Type* hashkey() const {
                if (!_is_Pointing())
                    return nullptr;
                return static_cast<Type*>(_ppobj->get());
            }

            Type* get() const { return alive() ? hashkey() : nullptr; }

            template <class Type2 = Type,
                      class = std::enable_if_t<!OWNER_PTR_IS_ARRAY(Type2)>>
            Type2* operator->() const {
                assert(_is_Pointing() && alive());
                return get();
            }

            template <class Type2 = Type,
                      class = std::enable_if_t<OWNER_PTR_IS_ARRAY(Type2)>>
            Type2& operator[](std::ptrdiff_t p) const {
                assert(_is_Pointing() && alive());
                return (*get())[p];
            }

            template <class Type2 = Type,
                      class = std::enable_if_t<!OWNER_PTR_IS_ARRAY(Type2)>>
            Type2& operator*() const {
                assert(_is_Pointing() && alive());
                return *get();
            }

            no_owner_ptr& operator=(const no_owner_ptr& r) {
                _detach(r._ppobj);
                return (*this);
            }

            no_owner_ptr& operator=(std::nullptr_t) {
                _detach();
                return (*this);
            }

            bool alive() const {
                return _is_Pointing() && _ppobj->alive() && _ppobj->get();
            }

            bool expired() const { return !alive(); }

    #ifdef PROXY_THREAD_SAFE
            std::optional<owner_shared_lock> try_shared_lock() {
                if (!_is_Pointing() || !alive())
                    return std::nullopt;
                return _ppobj->shared_lock();
            }

            std::optional<owner_lock> try_lock() {
                if (!_is_Pointing() || !alive())
                    return std::nullopt;
                return _ppobj->lock();
            }
    #endif

            ~no_owner_ptr() { _detach(); }

           protected:
            void _no_owner_from(const owner_ptr<_RTy, AtomicTypeFlag>& n) {
                _detach(n._state());
            }
            bool _is_Pointing() const { return _ppobj != nullptr; }
            void _detach(_common_PtrType* n = nullptr) {
                if (_ppobj) {
                    // decreasing no_owner ref count
                    _ppobj->dec_weak_ref();
                    // checking if object state has not reference
                    // (including no_ownerref)
                    if (!_ppobj->has_ref())
                        delete (_ppobj);
                }

                // updating new pointed object state
                _ppobj = n;
                if (_ppobj)
                    _ppobj->inc_weak_ref();
            }

           private:
            _common_PtrType* _ppobj = nullptr;
        };
    }  // namespace detail

    template <class Type, valid_atomic_flag AtomicFlag = owner_non_atomic>
    using weak_ptr = detail::no_owner_ptr<Type, AtomicFlag, ownership_link>;

    template <class Type, valid_atomic_flag AtomicFlag = owner_non_atomic>
    using proxy_ptr = detail::no_owner_ptr<Type, AtomicFlag, ownership_no_link>;

    // helper free function to make weak_ptr
    template <class Type, class AtomicFlag>
    weak_ptr<Type, AtomicFlag> make_weak(
        const owner_ptr<Type, AtomicFlag>& ptr) {
        return weak_ptr<Type, AtomicFlag>{ptr.get(), ptr._state()};
    }

    // helper free function to obtain ownership
    template <class Type, class AtomicFlag>
    owner_ptr<Type, AtomicFlag> get_ownership(
        const weak_ptr<Type, AtomicFlag>& ptr) {
        if (!ptr)
            return {};
        return owner_ptr<Type, AtomicFlag>{ptr.get(), ptr._state()};
    }

    namespace detail {
        // helper struct used to recognize
        // if a type is a owner library smart pointer
        template <class T> struct is_owner_smart_ptr {
            static bool constexpr value = false;
        };

        template <typename Type, typename AtomicType>
        struct is_owner_smart_ptr<owner_ptr<Type, AtomicType>> {
            static bool constexpr value = true;
        };
        template <typename Type, typename AtomicType>
        struct is_owner_smart_ptr<weak_ptr<Type, AtomicType>> {
            static bool constexpr value = true;
        };
        template <typename Type, typename AtomicType>
        struct is_owner_smart_ptr<proxy_ptr<Type, AtomicType>> {
            static bool constexpr value = true;
        };

        // helper struct used to detect cast destination type
        template <typename T, typename U> struct cast_dest_pointer_type;

        template <typename T, typename OT, typename AtomicType>
        struct cast_dest_pointer_type<owner_ptr<OT, AtomicType>, T> {
            using type = owner_ptr<T, AtomicType>;
        };
        template <typename T, typename OT, typename AtomicType>
        struct cast_dest_pointer_type<weak_ptr<OT, AtomicType>, T> {
            using type = weak_ptr<T, AtomicType>;
        };
        template <typename T, typename OT, typename AtomicType>
        struct cast_dest_pointer_type<proxy_ptr<OT, AtomicType>, T> {
            using type = proxy_ptr<T, AtomicType>;
        };
    }  // namespace detail

    // concept used to aggregate all classes in a single alias
    template <class T>
    concept owner_smart_ptr = detail::is_owner_smart_ptr<T>::value;

    // helper alias to autodetect smart pointer dest type
    template <typename T, typename P>
    using cast_dest_type = detail::cast_dest_pointer_type<T, P>;

    // defining smart pointer casts
    template <class T, owner_smart_ptr SP>
    cast_dest_type<SP, T> static_pointer_cast(const SP& r) noexcept {
        using destination_type = cast_dest_type<SP, T>;
        auto p = static_cast<typename destination_type::Type*>(r.get());
        return destination_type{p, r};
    }

    template <class T, owner_smart_ptr SP>
    cast_dest_type<SP, T> dynamic_pointer_cast(const SP& r) noexcept {
        using destination_type = cast_dest_type<SP, T>;
        if (auto p = dynamic_cast<typename destination_type::Type*>(r.get()))
            return destination_type{p, r};
        else
            return destination_type{};
    }

    template <class T, owner_smart_ptr SP>
    cast_dest_type<SP, T> const_pointer_cast(const SP& r) noexcept {
        using destination_type = cast_dest_type<SP, T>;
        auto p = const_cast<typename destination_type::Type*>(r.get());
        return destination_type{p, r};
    }

    template <class T, owner_smart_ptr SP>
    cast_dest_type<SP, T> reinterpret_pointer_cast(const SP& r) noexcept {
        using destination_type = cast_dest_type<SP, T>;
        auto p = reinterpret_cast<typename destination_type::Type*>(r.get());
        return destination_type{p, r};
    }

    template <class Type> class proxy_parent_base {
       public:
        proxy_ptr<Type> proxy() { return _make_proxy(); }
        proxy_ptr<Type> proxy_from_this() { return _make_proxy(); }
    #ifdef PROXY_THREAD_SAFE
        owner_lock lock() { return *_proxyGeneratorPtr.try_lock(); }
        owner_lock shared_lock() {
            return *_proxyGeneratorPtr.try_shared_lock();
        }
    #endif
        template <class Derived> proxy_ptr<Derived> proxy_from_base() {
            return owner::static_pointer_cast<Derived>(_make_proxy());
        }
        void proxy_delete() { _proxyGeneratorPtr.owner_delete(); }
        virtual ~proxy_parent_base() { proxy_delete(); }

       private:
        proxy_ptr<Type> _make_proxy() {
            return proxy_ptr<Type>{_proxyGeneratorPtr.get(),
                                   _proxyGeneratorPtr._state()};
        }

       private:
        owner_ptr<Type> _proxyGeneratorPtr{static_cast<Type*>(this),
                                           detail::non_deleter<Type>()};
    };

    namespace detail {
        template <class Ty, class Atomic> struct make_owner {
            template <class... args>
            static owner_ptr<Ty, Atomic> construct(const args&... va) {
                return owner_ptr<Ty, Atomic>{new Ty(va...)};
            }
        };

        template <class Ty, class Atomic> struct make_owner<Ty[], Atomic> {
            static owner_ptr<Ty[], Atomic> construct(size_t len) {
                return owner_ptr<Ty[], Atomic>{new Ty[len]};
            }
        };
    }  // namespace detail

    template <class Ty, class... Args>
    std::enable_if_t<detail::is_owner_valid_type<Ty>, owner_ptr<Ty>> make_owner(
        const Args&... Arguments) {
        return detail::make_owner<Ty, owner_non_atomic>::construct(
            Arguments...);
    }

    template <class Ty, class... Args>
    std::enable_if_t<detail::is_owner_valid_type<Ty>,
                     owner_ptr<Ty, owner_atomic>>
    make_owner_atomic(const Args&... Arguments) {
        return detail::make_owner<Ty, owner_atomic>::construct(Arguments...);
    }

    template <class Type, class AtomicType> struct owner_factory {
        template <class... args>
        static owner::owner_ptr<Type, AtomicType> make(const args&... arg) {
            return detail::make_owner<Type, AtomicType>::construct(arg...);
        }
    };

}  // namespace owner

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator==(const SP& _Left, std::nullptr_t) noexcept {
    return !_Left;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator==(std::nullptr_t,
                                     const SP& _Right) noexcept {
    return !_Right;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator!=(const SP& _Left,
                                     std::nullptr_t _Right) noexcept {
    return !(_Left == _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator!=(std::nullptr_t _Left,
                                     const SP& _Right) noexcept {
    return !(_Left == _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<(const SP& _Left,
                                    std::nullptr_t _Right) noexcept {
    using _Ptr = typename SP::pointer;
    return std::less<_Ptr>()(_Left.hashkey(), _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<(std::nullptr_t _Left,
                                    const SP& _Right) noexcept {
    using _Ptr = typename SP::pointer;
    return std::less<_Ptr>()(_Left, _Right.hashkey());
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>=(const SP& _Left,
                                     std::nullptr_t _Right) noexcept {
    return !(_Left < _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>=(std::nullptr_t _Left, const SP& _Right) {
    return !(_Left < _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>(const SP& _Left, std::nullptr_t _Right) {
    return _Right < _Left;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>(std::nullptr_t _Left, const SP& _Right) {
    return _Right < _Left;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<=(const SP& _Left, std::nullptr_t _Right) {
    return !(_Right < _Left);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<=(std::nullptr_t _Left, const SP& _Right) {
    return !(_Right < _Left);
}

//

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator==(
    const SP& _Left, const typename SP::Type* const _ptr) noexcept {
    return _Left.hashkey() == _ptr;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator==(const typename SP::Type* _ptr,
                                     const SP& _Right) noexcept {
    return _Right.hashkey() == _ptr;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator!=(
    const SP& _Left, const typename SP::Type* const _Right) noexcept {
    return !(_Left == _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator!=(const typename SP::Type* _Left,
                                     const SP& _Right) noexcept {
    return !(_Left == _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<(const SP& _Left,
                                    const typename SP::Type* const _Right) {
    using _Ptr = typename SP::pointer;
    return std::less<_Ptr>()(_Left.hashkey(), _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<(const typename SP::Type* const _Left,
                                    const SP& _Right) {
    using _Ptr = typename SP::pointer;
    return std::less<_Ptr>()(_Left, _Right.hashkey());
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>=(const SP& _Left,
                                     const typename SP::Type* const _Right) {
    return !(_Left < _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>=(const typename SP::Type* const _Left,
                                     const SP& _Right) {
    return !(_Left < _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>(const SP& _Left,
                                    const typename SP::Type* const _Right) {
    return _Right < _Left;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>(const typename SP::Type* const _Left,
                                    const SP& _Right) {
    return _Right < _Left;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<=(const SP& _Left,
                                     const typename SP::Type* const _Right) {
    return !(_Right < _Left);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<=(const typename SP::Type* const _Left,
                                     const SP& _Right) {
    return !(_Right < _Left);
}

//

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator==(const SP& _Left,
                                     typename SP::Type* const _ptr) noexcept {
    return _Left.hashkey() == _ptr;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator==(typename SP::Type* const _ptr,
                                     const SP& _Right) noexcept {
    return _Right.hashkey() == _ptr;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator!=(const SP& _Left,
                                     typename SP::Type* const _Right) noexcept {
    return !(_Left == _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator!=(typename SP::Type* const _Left,
                                     const SP& _Right) noexcept {
    return !(_Left == _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<(const SP& _Left,
                                    typename SP::Type* const _Right) {
    using _Ptr = typename SP::pointer;
    return std::less<_Ptr>()(_Left.hashkey(), _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<(typename SP::Type* const _Left,
                                    const SP& _Right) {
    using _Ptr = typename SP::pointer;
    return std::less<_Ptr>()(_Left, _Right.hashkey());
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>=(const SP& _Left,
                                     typename SP::Type* const _Right) {
    return !(_Left < _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>=(typename SP::Type* const _Left,
                                     const SP& _Right) {
    return !(_Left < _Right);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>(const SP& _Left,
                                    typename SP::Type* const _Right) {
    return _Right < _Left;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator>(typename SP::Type* const _Left,
                                    const SP& _Right) {
    return _Right < _Left;
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<=(const SP& _Left,
                                     typename SP::Type* const _Right) {
    return !(_Right < _Left);
}

template <owner::owner_smart_ptr SP>
OWNER_PTR_NO_DISCARD bool operator<=(typename SP::Type* const _Left,
                                     const SP& _Right) {
    return !(_Right < _Left);
}

template <owner::owner_smart_ptr SP> struct std::hash<SP> {
    size_t operator()(const SP _ptr) const {
        return reinterpret_cast<std::uintptr_t>(_ptr.hashkey());
    }
};

template <owner::owner_smart_ptr SP> struct std::less<SP> {
    bool operator()(const SP& lhs, const SP& rhs) const { return lhs < rhs; }
};

#endif  // INCLUDE_OWNER_OWNER_H
