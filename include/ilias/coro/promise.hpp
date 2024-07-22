#pragma once

/**
 * @file promise.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief All coroutines's promise base, CoroPromise
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
enum class CancelStatus : uint8_t {
    Pending, //< This cancel is still pending
    Done,    //< This cancel is done
};

/**
 * @brief The state of the coroutines
 * 
 */
enum class CoroState : uint8_t {
    Null,      //< This coroutine is not started, resume allowed
    Running,   //< This coroutine is running, no resume allowed
    Suspended, //< This coroutine is suspended, resume allowed
    Done,      //< This coroutine is done, no resume allowed
    // Null -> Running <-> Suspended -> Done
};


/**
 * @brief Helper class for switch to another coroutine handle and destroy the current one if needed
 * 
 */
class _SwitchCoroutine : public std::suspend_always {    
public:
    _SwitchCoroutine(std::coroutine_handle<> handle, bool destroy) noexcept : mHandle(handle), mDestroy(destroy) { }
    ~_SwitchCoroutine() = default;

    auto await_suspend(std::coroutine_handle<> handle) noexcept { 
        auto target = mHandle; //< Copy to stack
        if (mDestroy) [[unlikely]] { //< destroy current coroutine if needed
            handle.destroy();
        }
        return target; 
    }
private:
    std::coroutine_handle<> mHandle;
    bool                    mDestroy;
};

/**
 * @brief All Promise's base
 * 
 */
class CoroPromise {
public:
    CoroPromise() = default;
    CoroPromise(const CoroPromise &) = delete; 
    ~CoroPromise() = default;

    auto initial_suspend() noexcept {
        struct Awaiter {
            auto await_ready() const noexcept -> bool { return false; }
            auto await_suspend(std::coroutine_handle<>) const noexcept -> void { }
            auto await_resume() { ILIAS_ASSERT(self->mState == CoroState::Null); self->mState = CoroState::Running; }
            CoroPromise *self;
        };
        return Awaiter {this};
    }

    auto final_suspend() noexcept -> _SwitchCoroutine {
        mState = CoroState::Done; //< Done is still suspended
        if (mStopOnDone) [[unlikely]] {
            mStopOnDone->stop();
        }
        // If has someone is waiting us and he is suspended
        // We can not resume a coroutine which is not suspended, It will cause UB
        std::coroutine_handle<> switchTo = std::noop_coroutine();
        if (mPrevAwaiting) {
            ILIAS_ASSERT(mPrevAwaiting->isResumable());
            mPrevAwaiting->setResumeCaller(this);
            switchTo = mPrevAwaiting->handle();
        }
        return {switchTo, mDestroyOnDone};
    }

    /**
     * @brief Unhandled Exception on Coroutine, default in terminate
     * 
     */
    auto unhandled_exception() noexcept -> void {
        std::terminate();
    }

    /**
     * @brief Cancel the current coro
     * 
     * @return CancelStatus
     */
    auto cancel() -> CancelStatus {
        mCanceled = true;
        switch (mState) {
            //< If the coro is done, just return
            case CoroState::Done: return CancelStatus::Done;
            //< If the coro is still running, we can not notify it
            case CoroState::Running: return CancelStatus::Pending;
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
        return mState != CoroState::Null;
    }

    auto isSuspended() const -> bool {
        return mState == CoroState::Suspended;
    }

    auto isResumable() const -> bool {
        return mState == CoroState::Suspended || mState == CoroState::Null;
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
     * @return CoroPromise* 
     */
    auto resumeCaller() const -> CoroPromise * {
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
        if (suspended) {
            mState = CoroState::Suspended;
        }
        else {
            mState = CoroState::Running;
        }
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
    auto setResumeCaller(CoroPromise *caller) noexcept -> void {
        mResumeCaller = caller;
    }

    /**
     * @brief Set the Prev Awaiting object, the awaiting will be resumed when the task done
     * 
     * @param awaiting 
     */
    auto setPrevAwaiting(CoroPromise *awaiting) noexcept -> void {
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
    bool mCanceled = false;
    bool mDestroyOnDone = false;
    const char *mName = nullptr;
    CoroState mState = CoroState::Null;
    StopToken *mStopOnDone = nullptr; //< The token will be stop on this promise done
    EventLoop *mEventLoop = EventLoop::instance();
    CoroPromise *mPrevAwaiting = nullptr;
    CoroPromise *mResumeCaller = nullptr;
    std::coroutine_handle<> mHandle = nullptr;
};

using PromiseBase [[deprecated("Use CoroPromise instead")]] = CoroPromise;

ILIAS_NS_END