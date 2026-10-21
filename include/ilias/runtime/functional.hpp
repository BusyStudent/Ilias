// INTERNAL!!!
#pragma once

#include <ilias/defines.hpp>
#include <type_traits> // std::is_invocable_r_v
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
template <bool Noexcept, typename R, typename ...Args>
class SmallFunctionBase {
public:
    SmallFunctionBase(const SmallFunctionBase &) = default;
    SmallFunctionBase(std::nullptr_t) noexcept {} // Empty function
    SmallFunctionBase() noexcept = default;

    /**
     * @brief Construct a new Small Function object, the Fn must be small enough (<= pointer size)
     * 
     * @tparam Fn 
     */
    template <SmallCallable Fn> 
        requires(std::is_invocable_r_v<R, Fn, Args...>)
    SmallFunctionBase(Fn fn) noexcept {
        if constexpr (Noexcept) {
            static_assert(std::is_nothrow_invocable_v<Fn, Args...>, "The function must be noexcept");
        }
        mFn = &SmallFunctionBase::objectProxy<Fn>;
        ::memcpy(&mUser, &fn, sizeof(fn));
    }

    /**
     * @brief Construct a new Small Function object from the raw part
     * 
     * @param fn 
     * @param user 
     */
    SmallFunctionBase(R (*fn)(Args..., void *) noexcept(Noexcept), void *user) noexcept {
        mFn = fn;
        mUser = user;
    }

    auto toRaw() const noexcept {
        return std::pair{mFn, mUser};
    }

    // Invoke this function, UB on empty function
    auto operator ()(Args ...args) const noexcept(Noexcept) -> R {
        ILIAS_ASSERT(mFn, "The function can't be call with empty state");
        return mFn(std::forward<Args>(args)..., mUser);
    }

    // Default...
    auto operator =(const SmallFunctionBase &) -> SmallFunctionBase & = default;
    auto operator <=>(const SmallFunctionBase &) const noexcept = default;

    // Check if the function is not empty
    explicit operator bool() const noexcept {
        return mFn != nullptr;
    }
private:
    // The proxy used for small function object
    template <SmallCallable Fn>
    static auto objectProxy(Args ...args, void *user) noexcept(Noexcept) -> R {
        std::array<std::byte, sizeof(Fn)> storage;
        ::memcpy(&storage, &user, sizeof(Fn));

        auto fn = std::bit_cast<Fn>(storage);
        return fn(std::forward<Args>(args)...);
    }

    R   (*mFn)(Args..., void *) noexcept(Noexcept) = nullptr;
    void *mUser = nullptr;
};


/// @copydoc SmallFunctionBase
template <typename R, typename ...Args>
class SmallFunction<R(Args...)> : public SmallFunctionBase<false, R, Args...> {
public:
    using SmallFunctionBase<false, R, Args...>::SmallFunctionBase;

    auto operator<=>(const SmallFunction &) const noexcept = default;
};

/// @copydoc SmallFunctionBase
template <typename R, typename ...Args>
class SmallFunction<R(Args...) noexcept> : public SmallFunctionBase<true, R, Args...> {
public:
    using SmallFunctionBase<true, R, Args...>::SmallFunctionBase;

    auto operator<=>(const SmallFunction &) const noexcept = default;
};

} // namespace runtime

ILIAS_NS_END