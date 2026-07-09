// INTERNAL !!!
#pragma once

#include <ilias/runtime/functional.hpp> // SmallFunction
#include <ilias/defines.hpp>
#include <stop_token>
#include <memory>
#include <new>

ILIAS_NS_BEGIN

namespace runtime {

/// The StopToken, used to get notify of the stop
using StopToken = std::stop_token;

/// The StopSource, used to signal the stop request
using StopSource = std::stop_source;

template <typename Callable>
using StopCallback = std::stop_callback<Callable>;

/// Helper class for using std::stop_callback on Awaiter
template <typename Callable>
class StopCallbackEx {
public:
    using T = StopCallback<Callable>;

    StopCallbackEx() = default; // Runtime check ensures that only the empty state can be moved, make compiler happy :(
    StopCallbackEx(const StopCallbackEx &other) = delete;
    StopCallbackEx(StopCallbackEx &&other) noexcept { ILIAS_ASSERT(!other.mHasValue); }
    ~StopCallbackEx() { reset(); }

    auto operator =(const StopCallbackEx &other) -> StopCallbackEx & = delete;
    auto operator =(StopCallbackEx &&other) noexcept -> StopCallbackEx & { ILIAS_ASSERT(!other.mHasValue); return *this; }

    auto reset() -> void {
        if (mHasValue) {
            mHasValue = false;
            std::destroy_at(std::launder(reinterpret_cast<T *>(mData)));
        }
    }

    template <typename... Args>
    auto emplace(Args &&... args) -> void {
        ILIAS_ASSERT(!mHasValue);
        std::construct_at(reinterpret_cast<T *>(mData), std::forward<Args>(args)...);
        mHasValue = true;
    }
private:
    alignas(T) std::byte mData[sizeof(T)]; // TODO: Nice optimization:
    bool mHasValue = false;
};

/// Helper class for using std::stop_callback on Awaiter
class StopRegistration {
public:
    StopRegistration(const StopRegistration &) = delete;
    StopRegistration(StopRegistration &&) = default;
    StopRegistration() = default;

    auto operator =(const StopRegistration &) -> StopRegistration & = delete;
    auto operator =(StopRegistration &&) -> StopRegistration & = default;

    // NOLINTBEGIN
    // Do the registration with fn
    template <typename Token>
    auto register_(Token &&token, SmallFunction<void()> fn) -> void {
        mCallback.emplace(std::forward<Token>(token), fn);
    }

    // Do the registration with a member function
    template <auto Method, typename Object, typename Token>
    auto register_(Token &&token, Object *self) -> void {
        register_(std::forward<Token>(token), [self]() -> void {
            (self->*Method)(); // Call the method
        });
    }

    // NOLINTEND
    auto reset() -> void { mCallback.reset(); }
private:
    StopCallbackEx<SmallFunction<void()> > mCallback;
};

} // namespace runtime

ILIAS_NS_END