/**
 * @file cancellation_token.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief a way of register / unregister cancellation callback
 * @version 0.1
 * @date 2024-08-07
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/detail/functional.hpp> // for MoveOnlyFunction
#include <ilias/ilias.hpp>
#include <memory>
#include <list>

ILIAS_NS_BEGIN

class CancellationTokenRegistration;
class CancellationToken;

namespace detail {

/**
 * @brief The cancellation callback, it is a wrapper of the user callback, and has the iter of the callback in the token
 * 
 */
class CancellationCallback {
public:
    CancellationCallback(MoveOnlyFunction<void()> &&callback) : mCallback(std::move(callback)) { }
    CancellationCallback(const CancellationCallback &) = delete;
    ~CancellationCallback() {
        if (mList) {
            mIt = mList->erase(mIt);
        }
    }

    auto unlink() -> void {
        mIt = mList->end();
        mList = nullptr;
    }

    auto link(std::list<CancellationCallback*> &list, std::list<CancellationCallback*>::iterator it) -> void {
        ILIAS_ASSERT(!mList);
        mList = &list;
        mIt = it;
    }

    auto invoke() -> void {
        ILIAS_ASSERT(mCallback);
        mCallback();
    }
private:
    // Properties
    std::list<CancellationCallback*> *mList = nullptr;
    std::list<CancellationCallback*>::iterator mIt; //< Iterator of the callback in the token

    // Callable
    MoveOnlyFunction<void()> mCallback;
};

} // namespace detail


/**
 * @brief The Registration of cancellation token, it take the ownship of the callback, 
 *  user MUST NOT destroy it on the callback
 * 
 */
class CancellationTokenRegistration {
public:
    CancellationTokenRegistration() = default;

    explicit operator bool() const { return bool(mCallback); }
private:
    std::unique_ptr<detail::CancellationCallback> mCallback;
friend class CancellationToken;
};

/**
 * @brief Cancallation token, user can register a callback to be called when the token is cancelled
 * 
 */
class CancellationToken {
public:
    CancellationToken() = default;
    CancellationToken(const CancellationToken &) = delete;
    ~CancellationToken() {
        ILIAS_ASSERT(!mIsInCancelling); //< It is ill-formed to destroy the token in the callback
        for (auto &callback : mCallbacks) {
            callback->unlink();
        }
    }

    using Registration = CancellationTokenRegistration;

    
    /**
     * @brief Register a callback to be called when the token is cancelled
     * 
     * @param callback The callback, user MUST ensure the callback won't modify the token
     * @return Registration 
     */
    auto register_(detail::MoveOnlyFunction<void()> &&callback) -> Registration {
        Registration reg;

        if (mIsCancellationRequested) {
            callback();
            return reg;
        }
        ILIAS_ASSERT(!mIsInCancelling); //< It is ill-formed to register callback in the callback

        // Create the callback and register it
        auto ptr = std::make_unique<detail::CancellationCallback>(std::move(callback));
        auto it = mCallbacks.emplace(mCallbacks.end(), ptr.get());
        ptr->link(mCallbacks, it);

        reg.mCallback = std::move(ptr);
        return reg;
    }

    /**
     * @brief Register a callback to be called when the token is cancelled
     * 
     * @param fn The callback, user MUST ensure the callback won't modify the token
     * @param args 
     * @return Registration 
     */
    auto register_(void (*fn)(void *), void *args) -> Registration {
        return register_([fn, args]() {
            fn(args);
        });
    }

    /**
     * @brief Check if the cancellation is requested
     * 
     * @return true 
     * @return false 
     */
    [[deprecated("Use isCancellationRequested() instead")]]
    auto isCancelled() const -> bool {
        return mIsCancellationRequested;
    }

    /**
     * @brief Check if the cancellation is requested
     * 
     * @return true 
     * @return false 
     */
    auto isCancellationRequested() const -> bool {
        return mIsCancellationRequested;
    }

    /**
     * @brief Do the cancellation and invoke all the callbacks
     * 
     */
    auto cancel() -> void {
        if (mIsCancellationRequested) {
            return;
        }
        mIsCancellationRequested = true;
        mIsInCancelling = true; //< Prevent new callback to be registered

        for (auto &callback : mCallbacks) {
            callback->unlink();
            callback->invoke();
        }
        
        mIsInCancelling = false;
        mCallbacks.clear();
    }
private:
    bool mIsCancellationRequested = false;
    bool mIsInCancelling = false;
    std::list<detail::CancellationCallback*> mCallbacks; //< Invoke when token is cancelled
};


ILIAS_NS_END