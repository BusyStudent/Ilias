// INTERNAL !!!
#pragma once
#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/await.hpp>
#include <functional>
#include <coroutine> // std::coroutine_handle<>
#include <exception> // std::current_exception
#include <utility> // std::exchange

ILIAS_NS_BEGIN

// Runtime internal coroutine classes
namespace runtime {

// Helper class to switch between coroutines
class SwitchCoroutine {
public:
    SwitchCoroutine(std::coroutine_handle<> handle) : mHandle(handle) {}

    auto await_ready() noexcept { return false; }
    auto await_suspend(std::coroutine_handle<>) noexcept { return mHandle; }
    auto await_resume() noexcept {}
private:
    std::coroutine_handle<> mHandle;
};

// The Runtime environment for coroutines
class CoroContext {
public:
    CoroContext() = default;
    CoroContext(std::nostopstate_t) : mStopSource(std::nostopstate) {}

    // Request to stop the coroutine
    auto stop() noexcept {
        mStopSource.request_stop();
    }

    // Check if the coroutine is stopped
    auto isStopped() const noexcept {
        return mStopped;
    }

    auto executor() const noexcept -> Executor & {
        return *mExecutor;
    }

    auto stopSource() const noexcept -> const StopSource & {
        return mStopSource;
    }

    auto setExecutor(Executor &executor) noexcept {
        mExecutor = &executor;
    }

    auto setStoppedHandler(void (*handler)(CoroContext &)) noexcept {
        mStoppedHandler = handler;
    }
private:
    StopSource  mStopSource;                               // Use this to try to stop the coroutine
    Executor   *mExecutor = nullptr;
    void      (*mStoppedHandler)(CoroContext &) = nullptr; // Called when coroutine is stopped
    bool        mStopped = false;
friend class CoroHandle;
};

// The common part of all coroutines
class CoroPromise {
public:
    CoroPromise() = default;
    CoroPromise(const CoroPromise &) = delete;

    // std coroutine interface
    auto initial_suspend() noexcept -> std::suspend_always {
        return {};
    }

    auto final_suspend() noexcept -> SwitchCoroutine {
        if (mCompletionHandler) {
            mCompletionHandler(*mContext);
        }
        return {mPrevAwaiting};
    }

    auto unhandled_exception() noexcept { 
        mException = std::current_exception();
    }

    template <Awaitable T>
    auto await_transform(T &&awaitable) -> decltype(auto) { // We apply the environment on here
        if constexpr (requires { awaitable.setContext(*mContext); }) { // It support it
            awaitable.setContext(*mContext);
        }
        return std::forward<T>(awaitable);
    }

    template <AwaitTransformable T>
    auto await_transform(T &&transformable) {
        return await_transform(awaitTransform(std::forward<T>(transformable)));
    }

    // Our runtime interface
    auto rethrowIfNeeded() {
        if (mException) {
            std::rethrow_exception(std::exchange(mException, nullptr));
        }
    }
private:
    CoroContext       *mContext = nullptr;
    std::exception_ptr mException = nullptr;
    void (*mCompletionHandler)(CoroContext &) = nullptr; // Called when coroutine is completed, stopped is not completed
protected: // protected ..., write by Generator :(
    std::coroutine_handle<> mPrevAwaiting = std::noop_coroutine();
friend class CoroHandle;
};

// The common part handle of all coroutines
class CoroHandle {
public:
    template <typename T> requires (std::is_base_of_v<CoroPromise, T>)
    CoroHandle(std::coroutine_handle<T> handle) noexcept : mHandle(handle), mPromise(&handle.promise()) {}
    CoroHandle(const CoroHandle &) = default;
    CoroHandle(nullptr_t) {};
    CoroHandle() = default;

    // std coroutine interface
    auto done() const noexcept {
        return mHandle.done();
    }

    auto resume() const noexcept {
        ILIAS_ASSERT_MSG(!context().isStopped(), "Cannot resume a stopped coroutine");
        return mHandle.resume();
    }

    auto destroy() const noexcept {
        return mHandle.destroy();
    }

    template <typename T>
    auto promise() const noexcept -> T & {
        return static_cast<T &>(*mPromise);
    }

    // Our runtime interface
    auto context() const noexcept -> CoroContext & {
        return *(mPromise->mContext); // Context must be set before coroutine starts
    }

    auto executor() const noexcept -> Executor & {
        return *context().mExecutor; // Executor must not be null
    }

    auto setContext(CoroContext &ctxt) const noexcept {
        mPromise->mContext = &ctxt;
    }

    // Tell the context, are we stopped now, it only can called when the stop was requested and coroutine is suspended
    auto setStopped() const noexcept {
        auto &ctxt = context();
        ILIAS_ASSERT_MSG(ctxt.mStoppedHandler, "Stopped handler must be set, double call on CoroHandle::setStopped() ?");
        ILIAS_ASSERT_MSG(ctxt.mStopSource.stop_possible(), "Stop source must be possible to stop, invalid state ?");
        ILIAS_ASSERT_MSG(ctxt.mStopSource.stop_requested(), "Stop source must be requested, invalid state ?");
        ctxt.mStopped = true;
        ctxt.mStoppedHandler(ctxt); // Call the stopped handler, we are stopped
        ctxt.mStoppedHandler = nullptr; // Mark it as called
    }

    // Set the completion handler, it will be called when coroutine is completed, stopped is not completed
    auto setCompletionHandler(void (*handler)(CoroContext &)) const noexcept {
        mPromise->mCompletionHandler = handler;
    }

    // Set the previous awaiting coroutine, when the coroutine is completed, it will resume the previous awaiting coroutine
    auto setPrevAwaiting(CoroHandle h) const noexcept {
        mPromise->mPrevAwaiting = h.mHandle;
    }

    // Resume in the executor
    auto schedule() const noexcept {
        ILIAS_ASSERT_MSG(!context().isStopped(), "Cannot schedule a stopped coroutine");
        return executor().post(scheduleImpl, mHandle.address());
    }

    auto stopToken() const noexcept {
        return context().mStopSource.get_token();
    }

    auto isStopRequested() const noexcept {
        return context().mStopSource.stop_requested();
    }

    // Get the std coroutine handle
    auto toStd() const noexcept {
        return mHandle;
    }

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    static auto scheduleImpl(void *h) -> void {
        std::coroutine_handle<>::from_address(h).resume();
    }

    std::coroutine_handle<> mHandle;
    CoroPromise *mPromise = nullptr;
friend class std::hash<CoroHandle>;
};

} // namespace runtime

// Some builtin function for get environment
namespace runtime::context {

struct AwaiterBase {
    auto await_ready() noexcept { return true; }
    auto await_suspend(CoroHandle) noexcept {}
    auto setContext(CoroContext &ctxt) noexcept { mCtxt = &ctxt; }

    CoroContext *mCtxt = nullptr;
};

// Get the stop token from the current coroutine ctxt
inline auto stopToken() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept -> StopToken {
            return mCtxt->stopSource().get_token();
        }
    };

    return Awaiter {};
}

// Get the executor
inline auto executor() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept -> Executor & {
            return mCtxt->executor();
        }
    };

    return Awaiter {};
}

// Try set the context stopped, it only work when the stop was requested
inline auto stopped() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_ready() noexcept { // If stop requested, enter the stopped state
            return !mCtxt->stopSource().stop_requested();
        }
        auto await_suspend(CoroHandle h) noexcept {
            h.setStopped();
        }
        auto await_resume() noexcept {}
    };

    return Awaiter {};
}

// Yield the current coroutine, it will be resumed in the executor
inline auto yield() noexcept {
    struct Awaiter {
        auto await_ready() noexcept { return false; }
        auto await_suspend(CoroHandle h) noexcept { h.schedule(); }
        auto await_resume() noexcept {}
    };

    return Awaiter {};
}

} // namespace runtime::env

ILIAS_NS_END

// Interop with std...
template <>
struct std::hash<ILIAS_NAMESPACE::runtime::CoroHandle> {
    auto operator()(const ILIAS_NAMESPACE::runtime::CoroHandle &h) const noexcept -> std::size_t {
        return std::hash<std::coroutine_handle<> >()(h.mHandle);
    }
};