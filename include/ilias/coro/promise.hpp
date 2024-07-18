#pragma once

/**
 * @file promise.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief All coroutines's promise base, PromiseBase
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "loop.hpp"
#include <coroutine>
#include <concepts>

ILIAS_NS_BEGIN

/**
 * @brief Cancel status for cancel
 * 
 */
enum class CancelStatus {
    Pending, //< This cancel is still pending
    Done,    //< This cancel is done
};


/**
 * @brief Helper class for switch to another coroutine handle
 * 
 */
class SwitchCoroutine {
public:
    template <typename T>
    SwitchCoroutine(std::coroutine_handle<T> handle) noexcept : mHandle(handle) { }
    ~SwitchCoroutine() = default;

    auto await_ready() const noexcept -> bool { return false; }
    auto await_suspend(std::coroutine_handle<> handle) noexcept { return mHandle; }
    auto await_resume() const noexcept -> void { }
private:
    std::coroutine_handle<> mHandle;
};

/**
 * @brief All Promise's base
 * 
 */
class PromiseBase {
public:
    PromiseBase() = default;
    PromiseBase(const PromiseBase &) = delete; 
    ~PromiseBase() = default;

    auto initial_suspend() noexcept {
        struct Awaiter {
            auto await_ready() const noexcept -> bool { return false; }
            auto await_suspend(std::coroutine_handle<>) const noexcept -> void { self->mSuspended = true; }
            auto await_resume() { ILIAS_ASSERT(self); self->mStarted = true; self->mSuspended = false; }
            PromiseBase *self;
        };
        return Awaiter {this};
    }

    auto final_suspend() noexcept -> SwitchCoroutine {
        mSuspended = true; //< Done is still suspended
        if (mStopOnDone) [[unlikely]] {
            mStopOnDone->stop();
        }
        if (mDestroyOnDone) [[unlikely]] {
            mEventLoop->destroyHandle(mHandle);
        }
        // If has someone is waiting us and he is suspended
        // We can not resume a coroutine which is not suspended, It will cause UB
        if (mPrevAwaiting) {
            ILIAS_ASSERT(mPrevAwaiting->isResumable());
            mPrevAwaiting->setResumeCaller(this);
            return mPrevAwaiting->mHandle;
        }
        return std::noop_coroutine();
    }

    /**
     * @brief Unhandled Exception on Coroutine, default in terminate
     * 
     */
    auto unhandled_exception() noexcept -> void {
        std::terminate();
    }

    /**
     * @brief Cancel the current coroutine
     * 
     * @return CancelStatus
     */
    auto cancel() -> CancelStatus {
        mCanceled = true;
        if (!mSuspended) {
            return CancelStatus::Pending;
        }
        while (!mHandle.done()) {
            mHandle.resume();
        }
        return CancelStatus::Done;
    }

    auto eventLoop() const -> EventLoop * {
        return mEventLoop;
    }

    auto isCanceled() const -> bool {
        return mCanceled;
    }

    auto isStarted() const -> bool {
        return mStarted;
    }

    auto isSuspended() const -> bool {
        return mSuspended;
    }

    auto isResumable() const -> bool {
        return mSuspended && !mHandle.done();
    }

    auto name() const -> const char * {
        return mName;
    }

    auto handle() const -> std::coroutine_handle<> {
        return mHandle;
    }

    /**
     * @brief Get the pointer of the promise which resume us
     * 
     * @return PromiseBase* 
     */
    auto resumeCaller() const -> PromiseBase * {
        return mResumeCaller;
    }

    /**
     * @brief Set the Stop On Done object, the token's stop() method will be called when the promise done
     * 
     * @param token The token pointer
     */
    auto setStopOnDone(StopToken *token) noexcept -> void {
        mStopOnDone = token;
    }

    /**
     * @brief Set the Suspended object
     * @internal Don't use this, it's for internal use, by AwaitRecorder
     * 
     * @param suspended 
     */
    auto setSuspended(bool suspended) noexcept -> void {
        mSuspended = suspended;
    }

    /**
     * @brief Let it destroy on the coroutine done
     * 
     */
    auto setDestroyOnDone() noexcept -> void {
        mDestroyOnDone = true;
    }

    /**
     * @brief Set the Resume Caller object
     * 
     * @param caller Who resume us
     */
    auto setResumeCaller(PromiseBase *caller) noexcept -> void {
        mResumeCaller = caller;
    }

    /**
     * @brief Set the Prev Awaiting object, the awaiting will be resumed when the task done
     * 
     * @param awaiting 
     */
    auto setPrevAwaiting(PromiseBase *awaiting) noexcept -> void {
        mPrevAwaiting = awaiting;
    }

    /**
     * @brief Set the Event Loop object
     * 
     * @param eventLoop 
     */
    auto setEventLoop(EventLoop *eventLoop) noexcept -> void {
        mEventLoop = eventLoop;
    }
protected:
    bool mStarted = false;
    bool mCanceled = false;
    bool mSuspended = false; //< Is the coroutine suspended at await ?
    bool mDestroyOnDone = false;
    const char *mName = nullptr;
    StopToken *mStopOnDone = nullptr; //< The token will be stop on this promise done
    EventLoop *mEventLoop = EventLoop::instance();
    PromiseBase *mPrevAwaiting = nullptr;
    PromiseBase *mResumeCaller = nullptr;
    std::exception_ptr mException = nullptr;
    std::coroutine_handle<> mHandle = nullptr;
};

ILIAS_NS_END