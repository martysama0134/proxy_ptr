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

#include <type_traits>
#include <assert.h>
#include <atomic>

#define PROXY_PTR_NO_DISCARD [[nodiscard]]
#define PROXY_PTR_UNUSED(v) ((void)v)

namespace proxy {
    template <class Ty> class proxy_parent_base;  // forward declaration

    namespace detail {
        template <class _Ty, bool _IsArray> class common_ptr {
           private:
            _Ty* ptr = nullptr;
            size_t ref_Count = 0;

           public:
            common_ptr(_Ty* p) { ptr = p; }
            void inc_ref() { ref_Count++; }
            bool dec_ref() {
                if (ref_Count == 0)
                    return false;
                return --ref_Count != 0;
            }
            _Ty* get() const { return ptr; }
            _Ty* release() {
                auto old = ptr;
                ptr = nullptr;
                return old;
            }

            void delete_ptr() {
                if (ptr) {
                    if constexpr (_IsArray)
                        delete[] ptr;
                    else if constexpr (!_IsArray) {
                        delete ptr;
                    }
                    ptr = nullptr;
                }
            }

            ~common_ptr() { delete_ptr(); }
        };

        template <class Ty> struct _extra_proxy_pointer_type {
            using type = Ty*;
        };
        template <class Ty> struct _extra_proxy_pointer_type<Ty[]> {
            using type = Ty*;
        };

        template <class Ty>
        using extract_proxy_pointer_type =
            typename _extra_proxy_pointer_type<Ty>::type;

        template <class Ty>
        using extract_proxy_type =
            std::remove_pointer_t<extract_proxy_pointer_type<Ty>>;

        template <class Ty>
        constexpr bool is_proxy_valid_type =
            !std::is_array_v<Ty> ||
            (std::is_array_v<Ty> && std::extent_v<Ty> == 0);

    }  // namespace detail

    template <class _RTy> class proxy_ptr {
        using _Ty = detail::extract_proxy_type<_RTy>;
        using _common_Ptr_Ty = detail::common_ptr<_Ty, std::is_array_v<_RTy>>;

       protected:
        proxy_ptr(_common_Ptr_Ty* ptr) {
            _ppobj = ptr;
            if (_ppobj)
                _ppobj->inc_ref();
        }

       public:
        proxy_ptr() {}
        proxy_ptr(const proxy_ptr& n) { _proxy_from(n); }
        proxy_ptr(_Ty* r) { _detach(new _common_Ptr_Ty(r)); }
        proxy_ptr(std::nullptr_t) { _detach(); }

        operator bool() const { return (_is_Pointing() && _ppobj->get()); }
        explicit operator _Ty*() const { return get(); }

        _Ty* get() const {
            if (!_is_Pointing())
                return nullptr;
            return _ppobj->get();
        }

        _Ty* ptr() const { return get(); }

        template <class _Ty2 = _Ty,
                  class = std::enable_if_t<!std::is_array_v<_Ty2>>>
        _Ty2* operator->() const {
            assert(_is_Pointing());
            return get();
        }

        template <class _Ty2 = _Ty,
                  class = std::enable_if_t<std::is_array_v<_Ty2>>>
        _Ty2* operator[](std::ptrdiff_t p) const {
            assert(_is_Pointing());
            return (*get())[p];
        }

        template <class _Ty2 = _Ty,
                  class = std::enable_if_t<!std::is_array_v<_Ty2>>>
        _Ty2* operator*() const {
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
        template <class Ty> struct make_proxy {
            template <class... args>
            static proxy_ptr<Ty> construct(const args&... va) {
                return {new Ty(va...)};
            }
        };

        template <class Ty> struct make_proxy<Ty[]> {
            static proxy_ptr<Ty[]> construct(size_t len) {
                return {new Ty[len]};
            }
        };
    }  // namespace detail

    template <class Ty, class... Args>
    std::enable_if_t<detail::is_proxy_valid_type<Ty>, proxy_ptr<Ty>> make_proxy(
        const Args&... Arguments) {
        return detail::make_proxy<Ty>::construct(Arguments...);
    }

}  // namespace proxy

template <class _Ty1, class _Ty2>
PROXY_PTR_NO_DISCARD bool operator==(const proxy::proxy_ptr<_Ty1>& _Left,
                                     const proxy::proxy_ptr<_Ty2>& _Right) {
    return _Left.get() == _Right.get();
}

template <class _Ty1, class _Ty2>
PROXY_PTR_NO_DISCARD bool operator!=(const proxy::proxy_ptr<_Ty1>& _Left,
                                     const proxy::proxy_ptr<_Ty2>& _Right) {
    return !(_Left == _Right);
}

template <class _Ty1, class _Ty2>
PROXY_PTR_NO_DISCARD bool operator<(const proxy::proxy_ptr<_Ty1>& _Left,
                                    const proxy::proxy_ptr<_Ty2>& _Right) {
    using _Ptr1 = typename proxy::proxy_ptr<_Ty1>::pointer;
    using _Ptr2 = typename proxy::proxy_ptr<_Ty2>::pointer;
    using _Common = std::common_type_t<_Ptr1, _Ptr2>;
    return std::less<_Common>()(_Left.get(), _Right.get());
}

template <class _Ty1, class _Ty2>
PROXY_PTR_NO_DISCARD bool operator>=(const proxy::proxy_ptr<_Ty1>& _Left,
                                     const proxy::proxy_ptr<_Ty2>& _Right) {
    return !(_Left < _Right);
}

template <class _Ty1, class _Ty2>
PROXY_PTR_NO_DISCARD bool operator>(const proxy::proxy_ptr<_Ty1>& _Left,
                                    const proxy::proxy_ptr<_Ty2>& _Right) {
    return _Right < _Left;
}

template <class _Ty1, class _Ty2>
PROXY_PTR_NO_DISCARD bool operator<=(const proxy::proxy_ptr<_Ty1>& _Left,
                                     const proxy::proxy_ptr<_Ty2>& _Right) {
    return !(_Right < _Left);
}

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
                                    std::nullptr_t _Right) {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left.get(), _Right);
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator<(std::nullptr_t _Left,
                                    const proxy::proxy_ptr<_Ty>& _Right) {
    using _Ptr = typename proxy::proxy_ptr<_Ty>::pointer;
    return std::less<_Ptr>()(_Left, _Right.get());
}

template <class _Ty>
PROXY_PTR_NO_DISCARD bool operator>=(const proxy::proxy_ptr<_Ty>& _Left,
                                     std::nullptr_t _Right) {
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

template <class Type> class std::hash<proxy::proxy_ptr<Type>> {
    auto operator()(const proxy::proxy_ptr<Type> ptr) { return ptr.get(); }
};

template <class Type> struct std::less<proxy::proxy_ptr<Type>> {
    bool operator()(const proxy::proxy_ptr<Type>& lhs,
                    const proxy::proxy_ptr<Type>& rhs) const {
        return lhs < rhs;
    }
};
