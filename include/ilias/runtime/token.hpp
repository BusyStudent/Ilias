// INTERNAL !!!
#pragma once

#include <ilias/defines.hpp>
#include <stop_token>
#include <memory>

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
    StopCallbackEx(StopCallbackEx &&other) { ILIAS_ASSERT(!other.mHasValue); }
    ~StopCallbackEx() { reset(); }

    auto operator =(const StopCallbackEx &other) -> StopCallbackEx & = delete;
    auto operator =(StopCallbackEx &&other) -> StopCallbackEx & { ILIAS_ASSERT(!other.mHasValue); return *this; }

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
    alignas(T) std::byte mData[sizeof(T)];
    bool mHasValue = false;
};

/// Helper class for using std::stop_callback on Awaiter
class StopRegistration {
public:
    StopRegistration(const StopRegistration &) = delete;
    StopRegistration(StopRegistration &&) = default;
    StopRegistration() = default;

    StopRegistration &operator=(const StopRegistration &) = delete;
    StopRegistration &operator=(StopRegistration &&) = default;

    // Do the registration
    auto register_(const StopToken &token, void (*fn)(void *), void *args) -> void {
        mCallback.emplace(token, Callback{fn, args});
    }
    auto register_(StopToken &&token, void (*fn)(void *), void *args) -> void {
        mCallback.emplace(std::move(token), Callback{fn, args});
    }

    // Do the registration with a member function
    template <auto Method, typename Object>
    auto register_(const StopToken &token, Object *self) -> void {
        mCallback.emplace(token, Callback{methodProxy<Method, Object>, self});
    }
    template <auto Method, typename Object>
    auto register_(StopToken &&token, Object *self) -> void {
        mCallback.emplace(std::move(token), Callback{methodProxy<Method, Object>, self});
    }

    auto reset() -> void { mCallback.reset(); }
private:
    struct Callback {
        void (*fn)(void *);
        void *args;

        void operator()() const { fn(args); }
    };

    template <auto Method, typename Object>
    static auto methodProxy(void *_self) -> void {
        auto self = static_cast<Object *>(_self);
        (self->*Method)();
    }

    StopCallbackEx<Callback> mCallback;
};

} // namespace runtime

ILIAS_NS_END