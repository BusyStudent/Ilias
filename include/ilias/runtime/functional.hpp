// INTERNAL!!!
#pragma once

#include <ilias/defines.hpp>
#include <functional>
#include <concepts>
#include <cstring> // memcpy
#include <array> // std::array
#include <bit> // std::bit_cast

ILIAS_NS_BEGIN

namespace runtime {

template <typename T>
class SmallFunction;

/**
 * @brief Check if the type small enough to be used as a small function
 * 
 * @tparam Fn 
 */
template <typename Fn>
concept SmallCallable = 
    std::is_trivially_copyable_v<Fn> &&
    std::is_trivially_destructible_v<Fn> &&
    sizeof(Fn) <= sizeof(void *)
;

/**
 * @brief The safe wrapper for RetT (*)(Args..., void *), void *, safer for handling the extra void * argument
 * 
 * @tparam R 
 * @tparam Args 
 */
template <typename R, typename ...Args>
class SmallFunction<R (Args...)> {
public:    
    SmallFunction(const SmallFunction &) = default;
    SmallFunction(SmallFunction &&) = default;
    SmallFunction(std::nullptr_t) noexcept {} // Empty function
    SmallFunction() = default;

    /**
     * @brief Construct a new Small Function object, the Fn must be small enough (<= pointer size)
     * 
     * @tparam Fn 
     */
    template <SmallCallable Fn> 
        requires(std::is_invocable_r_v<R, Fn, Args...>)
    SmallFunction(Fn fn) noexcept {
        mFn = &SmallFunction::objectProxy<Fn>;
        ::memcpy(&mUser, &fn, sizeof(fn));
    }

    /**
     * @brief Construct a new Small Function object from the raw part
     * 
     * @param fn 
     * @param user 
     */
    SmallFunction(R (*fn)(Args..., void *), void *user) noexcept {
        mFn = fn;
        mUser = user;
    }

    /**
     * @brief Construct a new Small Function object, the fn is the no void * version of the function
     * 
     * @param fn 
     */
    SmallFunction(R (*fn)(Args...)) noexcept {
        mFn = &SmallFunction::fnProxy;
        mUser = std::bit_cast<void *>(fn);
    }

    auto toRaw() const noexcept {
        return std::pair(mFn, mUser);
    }

    // Invoke this function, UB on empty function
    auto operator ()(Args ...args) const -> R {
        return mFn(std::forward<Args>(args)..., mUser);
    }

    // Default...
    auto operator =(const SmallFunction &) -> SmallFunction & = default;
    auto operator =(SmallFunction &&) -> SmallFunction & = default;
    auto operator <=>(const SmallFunction &) const noexcept = default;

    // Check if the function is not empty
    explicit operator bool() const noexcept {
        return mFn != nullptr;
    }
private:
    // The proxy used for small function object
    template <SmallCallable Fn>
    static auto objectProxy(Args ...args, void *user) -> R {
        std::array<std::byte, sizeof(Fn)> storage;
        ::memcpy(&storage, &user, sizeof(Fn));

        auto fn = std::bit_cast<Fn>(storage);
        return fn(std::forward<Args>(args)...);
    }

    // The proxy used for raw function pointer 
    static auto fnProxy(Args ...args, void *user) -> R {
        auto fn = std::bit_cast<R (*)(Args...)>(user);
        return (*fn)(std::forward<Args>(args)...);
    }

    R   (*mFn)(Args..., void *) = nullptr;
    void *mUser = nullptr;
};

} // namespace runtime

ILIAS_NS_END