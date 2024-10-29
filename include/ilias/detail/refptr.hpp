#pragma once

#include <ilias/ilias.hpp>
#include <concepts>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Check a type has instrusive reference counting
 * 
 * @tparam T 
 */
template <typename T>
concept RefCounted = requires(T& t) {
    t.ref();
    t.deref();
};

/**
 * @brief The default reference counting trait, using the ref and deref method
 * 
 */
class RefCountedTrait {
public:
    template <RefCounted T>
    static auto ref(T *ptr) -> void {
        ptr->ref();
    }
    template <RefCounted T>
    static auto deref(T *ptr) -> void {
        ptr->deref();
    }
};

/**
 * @brief The reference counting pointer, use instrusive reference counting
 * 
 * @tparam T The type of the pointer
 * @tparam Trait The reference counting trait to add reference counting and dereference
 */
template <typename T, typename Trait = RefCountedTrait>
struct RefPtr {
public:
    RefPtr() = default;
    RefPtr(std::nullptr_t) { }
    RefPtr(const RefPtr &other) : mPtr(other.mPtr) {
        ref();
    }
    RefPtr(RefPtr &&other) : mPtr(other.mPtr) {
        other.mPtr = nullptr;
    }
    RefPtr(T *ptr) : mPtr(ptr) {
        ref();
    }
    ~RefPtr() {
        deref();
    }
    
    auto get() const noexcept {
        return mPtr;
    }

    auto operator =(const RefPtr &other) -> RefPtr & {
        deref();
        mPtr = other.mPtr;
        ref();
        return *this;
    }
    auto operator =(RefPtr &&other) -> RefPtr & {
        deref();
        mPtr = other.mPtr;
        other.mPtr = nullptr;
        return *this;
    }
    auto operator =(std::nullptr_t) -> RefPtr & {
        deref();
        mPtr = nullptr;
        return *this;
    }

    auto operator ->() const noexcept { return mPtr; }

    auto operator <=>(const RefPtr &other) const noexcept = default;

    operator bool() const noexcept { return mPtr != nullptr; }
private:
    auto ref() -> void {
        if (mPtr) {
            Trait::ref(mPtr);
        }
    }
    auto deref() -> void {
        if (mPtr) {
            Trait::deref(mPtr);
        }
    }

    T *mPtr = nullptr;
};

} // namespace detail

ILIAS_NS_END