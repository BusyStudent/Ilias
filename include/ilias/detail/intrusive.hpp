// INTERNAL!!!
#pragma once

#include <ilias/defines.hpp>
#include <concepts>
#include <utility> // std::forward
#include <memory> // std::pointer_traits

ILIAS_NS_BEGIN

namespace intrusive {

///
///   +---------------------------------------+
///   |                                       |
///   v                                       |
/// Sentinel -> Node1 -> Node2 -> Node3 ------+
///
class NodeBase {
public:
    NodeBase() = default;
    NodeBase(const NodeBase &) = delete;
    NodeBase(NodeBase &&other) noexcept {
        if (!other.isLinked()) {
            mNext = this;
            mPrev = this;
            return;
        }
        // Take from other
        mPrev = other.mPrev;
        mNext = other.mNext;

        // Bind the neighbors to self
        mPrev->mNext = this;
        mNext->mPrev = this;

        // Unbind other
        other.mPrev = &other;
        other.mNext = &other;
    }
    ~NodeBase() {
        unlink();
    }

    // Check self is linked in a chain
    [[nodiscard]]
    auto isLinked() const noexcept -> bool {
        return mPrev != this || mNext != this;
    }

    // Unlink self from this chain
    auto unlink() noexcept -> void {
        if (!isLinked()) {
            return;
        }
        mPrev->mNext = mNext;
        mNext->mPrev = mPrev;
        mPrev = this;
        mNext = this;
    }

    // Insert self after the where node
    auto insertAfter(NodeBase *where) noexcept -> void {
        unlink();

        // where -> this -> next
        mNext = where->mNext;
        mPrev = where;

        where->mNext = this;
        mNext->mPrev = this;
    }

    // Insert self before the where node
    auto insertBefore(NodeBase *where) noexcept -> void {
        unlink();

        // prev -> this -> where
        mNext = where;
        mPrev = where->mPrev;

        where->mPrev = this;
        mPrev->mNext = this;
    }

    // Internal Traverse, make sure only this class can to link & unlink
    auto next() const noexcept {
        return mNext;
    }

    auto prev() const noexcept {
        return mPrev;
    }

    auto operator =(const NodeBase &) = delete;
    auto operator =(NodeBase &&) = delete;
private:
    NodeBase *mPrev = this;
    NodeBase *mNext = this;
};

// The List Sentinel
class ListBase : public NodeBase {
public:
    ListBase() = default;
    ListBase(ListBase &&) = default;
    ~ListBase() { clear(); }

    // Check the list is empty
    [[nodiscard]]
    auto empty() const -> bool {
        return next() == this;
    }

    // Clear the whole list
    auto clear() -> void {
        while (!empty()) {
            next()->unlink();
        }
    }

    // Get the size of the list， O(n), only for debug
    [[nodiscard]]
    auto size() const -> size_t {
        size_t n = 0;
        for (auto cur = next(); cur != this; cur = cur->next()) {
            n += 1;
        }
        return n;
    }
};

// User API
template <typename T>
class Node : protected NodeBase {
public:
    Node() = default;
    Node(Node &&) = default;
    ~Node() = default;

    // Re-export it
    using NodeBase::isLinked;
    using NodeBase::unlink;

template <typename U>
friend class List;
};

// The instrusive list, it doesn't take the ownship of the nodes
template <typename T>
class List final : protected ListBase {
public:
    static_assert(std::is_base_of_v<Node<T>, T>, "The element T must be derived from Node<T>");

    List() = default;
    List(List &&) = default;
    ~List() = default;

    template <bool Const>
    struct Iterator {
        using Ptr = std::conditional_t<Const, const NodeBase *, NodeBase *>;
        using Element = std::conditional_t<Const, const T, T>;

        auto operator ++() -> Iterator & { mCur = mCur->next(); return *this; }
        auto operator --() -> Iterator & { mCur = mCur->prev(); return *this; }
        auto operator *() const -> Element & { return static_cast<Element &>(*mCur); }
        auto operator ->() const -> Element * { return static_cast<Element *>(mCur); }
        auto operator <=>(const Iterator &) const noexcept = default;

        Ptr mCur = nullptr;
    };

    // For STL
    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    // Re-export it
    using ListBase::clear;
    using ListBase::empty;
    using ListBase::size;

    // Iterate the list
    auto begin() -> iterator { return {next()}; }
    auto end() -> iterator { return {this}; }

    auto begin() const -> const_iterator { return {next()}; }
    auto end() const -> const_iterator { return {this}; }

    // Insert
    auto push_back(T &node) -> void { static_cast<Node<T> &>(node).insertBefore(this); }
    auto push_front(T &node) -> void { static_cast<Node<T> &>(node).insertAfter(this); }

    auto pop_front() -> void {
        ILIAS_ASSERT(!empty());
        next()->unlink();
    }

    auto pop_back() -> void {
        ILIAS_ASSERT(!empty());
        prev()->unlink();
    }

    // Access
    auto front() -> T & { 
        ILIAS_ASSERT(!empty()); 
        return *begin(); 
    }
    auto back() -> T & { 
        ILIAS_ASSERT(!empty()); 
        return *(--end());
    }

    auto front() const -> const T & { 
        ILIAS_ASSERT(!empty()); 
        return *begin();
    }
    auto back() const -> const T & { 
        ILIAS_ASSERT(!empty());
        return *(--end()); 
    }

    // Erase current position, unlink the node
    auto erase(iterator position) -> iterator {
        auto next = position;
        ++next;
        position->unlink();
        return next;
    }
};

// The refcounted object, note that it's not thread safe
template <typename T>
class RefCounted {
public:
    RefCounted(const RefCounted &) = delete;

    // Add refcount 
    auto ref() noexcept {
        mCount += 1;
    }

    // Count down refcount
    auto deref() noexcept {
        ILIAS_ASSERT(mCount != 0, "Can't deref with refcount == 0, invalid state?");
        if (--mCount == 0) {
            delete static_cast<T*>(this);
        }
    }

    // Get the refcount, as same as STL
    [[nodiscard]]
    auto use_count() const noexcept {
        return mCount;
    }
protected:
    RefCounted() = default; // Disallow instantiated directly
    ~RefCounted() = default; // Disallow to delete it by base class
private:
    size_t mCount = 0;
};

// If user has specified the refcounted object
template <typename T>
concept RefCountedLike = requires(T &t) {
    { t.ref() } -> std::same_as<void>;
    { t.deref() } -> std::same_as<void>;
    { t.use_count() } -> std::same_as<size_t>;
};

// The smart pointer of the refcounted object
template <RefCountedLike T>
class Rc final {
public:
    Rc() = default;
    Rc(const Rc &other) noexcept : Rc(other.mPtr) {}
    Rc(Rc &&other) noexcept : mPtr(other.mPtr) { other.mPtr = nullptr; }
    Rc(T *obj) noexcept { reset(obj); }
    Rc(std::nullptr_t) noexcept {}
    ~Rc() { reset(); }

    // Clear the Rc, and take the ownhip of the newObject (nullptr is ok)
    auto reset(T *newObject = nullptr) noexcept -> void {
        if (newObject) {
            newObject->ref();
        }
        if (mPtr) {
            mPtr->deref();
        }
        mPtr = newObject;
    }

    // Get the raw pointer
    [[nodiscard]]
    auto get() const noexcept -> T * {
        return mPtr;
    }

    // Get the refcount value, as same as STL
    [[nodiscard]]
    auto use_count() const noexcept -> size_t {
        return mPtr ? mPtr->use_count() : 0;
    }

    // Swap
    auto swap(Rc &other) noexcept -> void {
        return std::swap(mPtr, other.mPtr);
    }

    // Operators...
    auto operator <=>(const Rc &other) const noexcept = default;

    auto operator =(const Rc &other) noexcept -> Rc & {
        if (this != &other) {
            reset(other.mPtr);
        }
        return *this;
    }
    
    auto operator =(Rc &&other) noexcept -> Rc & {
        if (this != &other) {
            reset();
            mPtr = std::exchange(other.mPtr, nullptr);
        }
        return *this;
    }

    auto operator ->() const noexcept -> T * {
        return mPtr;
    }
    auto operator *() const noexcept ->  T & {
        return *mPtr;
    }

    // Check the rc is empty?
    explicit operator bool() const noexcept {
        return mPtr != nullptr;
    }

    // Create an object by args
    template <typename ...Args>
    static auto make(Args &&...args) -> Rc<T> {
        return Rc<T> {
            new T (std::forward<Args>(args)...)
        };
    }
private:
    T *mPtr = nullptr;
};

} // namespace intrusive

ILIAS_NS_END

// Interop with std
template <typename T>
struct std::pointer_traits<ilias::intrusive::Rc<T> > {
    using pointer = ilias::intrusive::Rc<T>;
    using element_type = T;
    using difference_type = ptrdiff_t;

    template<typename U>
    using rebind = ilias::intrusive::Rc<U>;

    static auto to_address(const pointer &p) noexcept {
        return p.get();
    }
};