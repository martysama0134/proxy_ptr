#include <type_traits>
#include <functional>

namespace recursive_ptr {
    template <class _Ty>
    struct recursive_default_delete {  // default deleter for unique_ptr
        constexpr recursive_default_delete() noexcept = default;

        template <class _Ty2,
                  std::enable_if_t<std::is_convertible_v<_Ty2*, _Ty*>, int> = 0>
        recursive_default_delete(
            const recursive_default_delete<_Ty2>&) noexcept {}

        void operator()(_Ty* _Ptr) const noexcept
        /* strengthened */ {  // delete a pointer
            static_assert(0 < sizeof(_Ty), "can't delete an incomplete type");
            delete _Ptr;
        }
    };

    template <class _Ty>
    struct recursive_default_delete<_Ty[]> {  // default deleter for unique_ptr
                                              // to array of unknown size
        constexpr recursive_default_delete() noexcept = default;

        template <class _Uty,
                  std::enable_if_t<std::is_convertible_v<_Uty (*)[], _Ty (*)[]>,
                                   int> = 0>
        recursive_default_delete(
            const recursive_default_delete<_Uty[]>&) noexcept {}

        template <class _Uty,
                  std::enable_if_t<std::is_convertible_v<_Uty (*)[], _Ty (*)[]>,
                                   int> = 0>
        void operator()(_Uty* _Ptr) const noexcept
        /* strengthened */ {  // delete a pointer
            static_assert(0 < sizeof(_Uty), "can't delete an incomplete type");
            delete[] _Ptr;
        }
    };

    template <class _Ty, class _Dx = recursive_default_delete<_Ty>>
    class recursive_ptr {
       private:
        using rawptr_t = _Ty*;
        using object_t = _Ty;
        using nodeptr_t = recursive_ptr<_Ty>*;
        using noderef_t = recursive_ptr<_Ty>&;
        using moveref_t = recursive_ptr<_Ty>&&;
        using deleter_t = _Dx;
        using notifier_t = std::function<void(const noderef_t)>;

       public:
        recursive_ptr() {}
        recursive_ptr(rawptr_t ptr) { this->_ptr = ptr; }
        recursive_ptr(const noderef_t parent) { parent._add_child(this); }
        recursive_ptr(moveref_t other) { other._rewind(this); }
        recursive_ptr(rawptr_t ptr, deleter_t deleter) {
            this->_ptr = ptr;
            this->_deleter = deleter;
        }

        auto& operator=(const noderef_t parent) {
            parent._add_child(this);
            return *this;
        }

        auto& operator=(moveref_t other) { other._rewind(this); }

        auto& operator=(nullptr_t) noexcept {
            reuse(nullptr);
            return *this;
        }

        rawptr_t ptr() { return this->_ptr; }

        void recursive_assign(rawptr_t ptr) {
            _delete_ptr();

            this->_ptr = ptr;
            if (this->_child)
                this->_child->_recursive_assign_children(ptr);
            if (this->_parent)
                this->_parent->_recursive_assign_parent(ptr);
            _notify();
        }

        void recursive_delete() {
            if (_delete_ptr()) {
                if (this->_child)
                    this->_child->_recursive_assign_children(nullptr);
                if (this->_parent)
                    this->_parent->_recursive_assign_parent(nullptr);
            }

            _notify();
        }

        void reuse(rawptr_t ptr) {
            if (this->_parent)
                this->_parent->_remove_child();
            if (this->_child)
                this->_child->_set_parent(this->_parent);
            this->_ptr = ptr;
        }

        void reuse(rawptr_t ptr, deleter_t deleter) {
            if (this->_parent)
                this->_parent->_remove_child();

            if (this->child)
                this->_child->_set_parent(this->_parent);

            this->_ptr = ptr;
            this->_deleter = deleter;
        }

        void set_notifier(const notifier_t& notifier) {
            this->_notifier = notifier;
        }

        ~recursive_ptr() { reuse(nullptr); }

       private:
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

        void _set_ptr(rawptr_t ptr) { this->_ptr = ptr; }

        void _remove_child() const {
            if (this->_child)
                this->_child = this->_child->_get_child();
        }

        void _add_child(nodeptr_t child) {
            if (this->_child)
                this->_child->_add_child(child);

            else {
                this->_child = child;
                child->_set_parent(this);
                child->_set_ptr(this->_ptr);
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

        void _rewind(nodeptr_t newptr) {
            auto parent = this->_parent;
            auto child = this->_child;
            _orphanize_uncheking();
            if (child)
                child->_set_parent(newptr);
            if (parent)
                parent->_replace_child(newptr);
        }

        void _recursive_assign_children(rawptr_t ptr) {
            this->_ptr = ptr;
            if (this->_child)
                this->_recursive_assign_children(ptr);
        }

        void _recursive_assign_parent(rawptr_t ptr) {
            this->_ptr = ptr;
            if (this->_parent)
                this->_recursive_assign_parent(ptr);
        }

        void _notify() {
            if (this->_notifier)
                this->_notifier(*this);
        }

        bool _delete_ptr() {
            if (this->_ptr) {
                _get_deleter()(this->_ptr);
                this->_ptr = nullptr;
                return true;
            }

            return false;
        }

       private:
        mutable nodeptr_t _parent = nullptr;
        mutable nodeptr_t _child = nullptr;
        rawptr_t _ptr = nullptr;
        deleter_t _deleter = deleter_t();
        notifier_t _notifier = notifier_t();
    };

    template <class _Ty, class... TyArgs,
              std::enable_if_t<!std::is_array<_Ty>::value, int> = 0>
    recursive_ptr<_Ty> make_recursive(TyArgs&&... VArgs) {
        return std::move(
            recursive_ptr<_Ty>(new _Ty(std::forward<TyArgs>(VArgs)...)));
    }

    template <class _Ty,
              std::enable_if_t<std::is_array_v<_Ty> && std::extent_v<_Ty> == 0,
                               int> = 0>
    recursive_ptr<_Ty> make_recursive(const std::size_t size) {
        return std::move(recursive_ptr<_Ty>(new _Ty[size]));
    }

    template <class _Ty, class = std::enable_if_t<std::extent_v<_Ty> != 0, int>>
    recursive_ptr<_Ty> make_recursive(const std::size_t) = delete;

}  // namespace recursive_ptr

// open questions :
// 1. head destructor call must delete the pointer and move to null all the
// nodes? or would it just move the next node as new head?
