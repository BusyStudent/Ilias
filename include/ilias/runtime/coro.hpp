// INTERNAL !!!
#pragma once
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/capture.hpp> // CaptureSource, StackFrame
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/runtime/await.hpp> // Awaitable
#include <coroutine> // std::coroutine_handle<>
#include <exception> // std::current_exception
#include <concepts> // std::invocable
#include <utility> // std::exchange

ILIAS_NS_BEGIN

// Runtime internal coroutine classes
namespace runtime {

// Memory pool for coroutines
extern auto ILIAS_API allocate(size_t n) -> void *;
extern auto ILIAS_API deallocate(void *ptr, size_t n) noexcept -> void;

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
        return mStopSource.request_stop();
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

    auto userdata() const noexcept -> void * {
        return mUser;
    }

    auto setExecutor(Executor &executor) noexcept {
        mExecutor = &executor;
    }

    auto setStoppedHandler(void (*handler)(CoroContext &)) noexcept {
        mStoppedHandler = handler;
    }

    auto setUserdata(void *user) noexcept {
        mUser = user;
    }


    // TRACING: Set the parent of the ctxt
    auto setParent(CoroContext &parent) noexcept {
#if defined(ILIAS_CORO_TRACE)
        mParent = &parent;
#else
        static_cast<void>(parent);
#endif
    }

    // TRACING: Push the frame to the stack, return the index of the frame
    template <typename ...Args>
    auto pushFrame(Args &&...args) noexcept {
#if defined(ILIAS_CORO_TRACE)
        mFrames.emplace_back(std::forward<Args>(args)...);
        return mFrames.size() - 1;
#else
        (static_cast<void>(args), ...);
        return 0;
#endif // defined(ILIAS_CORO_TRACE)
    }

    // TRACING: Pop the frame from the stack
    auto popFrame() noexcept {
#if defined(ILIAS_CORO_TRACE)
        ILIAS_ASSERT(!mFrames.empty());
        mFrames.pop_back();
#endif // defined(ILIAS_CORO_TRACE)
    }

    // TRACING: Get the top frame of the ctxt (return pointer, nullptr on empty)
    auto topFrame() noexcept {
#if defined(ILIAS_CORO_TRACE)
        return mFrames.empty() ? nullptr : &mFrames.back();
#else
        return nullptr;
#endif // defined(ILIAS_CORO_TRACE)
    }

    // TRACING: Get the stacktrace of the ctxt
    auto stacktrace() const noexcept {
#if defined(ILIAS_CORO_TRACE)
        auto vec = std::vector<StackFrame> {};
        for (auto cur = this; cur != nullptr; cur = cur->mParent) {
            vec.insert(vec.end(), cur->mFrames.rbegin(), cur->mFrames.rend());
        }
        return Stacktrace { std::move(vec) };
#else
        return Stacktrace {};
#endif // defined(ILIAS_CORO_TRACE)
    }

    // Memory pool for coroutines (maybe.)
    auto operator new(size_t n) -> void * {
        return allocate(n);
    }

    auto operator delete(void *ptr, size_t n) noexcept -> void {
        return deallocate(ptr, n);
    }
private:
    StopSource    mStopSource;                               // Use this to try to stop the coroutine
    Executor     *mExecutor = nullptr;
    void        (*mStoppedHandler)(CoroContext &) = nullptr; // Called when coroutine is stopped
    void         *mUser = nullptr;                           // The user data, useful in the callback
    bool          mStopped = false;
#if defined(ILIAS_CORO_TRACE)
    CoroContext  *mParent = nullptr;                         // Use for stacktrace to dump the whole stack
    std::vector<StackFrame> mFrames;                         // The frames of the coroutine,
#endif // defined(ILIAS_CORO_TRACE)
friend class CoroHandle;
};

// The common part of all stackless coroutines
class CoroPromise {
public:
    CoroPromise() = default;
    CoroPromise(const CoroPromise &) = delete;
    ~CoroPromise() noexcept = default;

    // std coroutine interface
    auto initial_suspend() noexcept {
        struct Awaiter {
            auto await_ready() noexcept { return false; }
            auto await_suspend(std::coroutine_handle<> handle) noexcept {}
            auto await_resume() noexcept { self.init(); }
            CoroPromise &self;
        };
        return Awaiter {*this};
    }

    auto final_suspend() noexcept -> SwitchCoroutine {
        if (mCompletionHandler) {
            mCompletionHandler(*mContext);
        }
        // TRACING: Cleanup the frame belong to us
        mContext->popFrame();
        return {mPrevAwaiting};
    }

    auto unhandled_exception() noexcept { 
        mException = std::current_exception();
    }

    template <RawAwaitable T>
    auto await_transform(T &&awaitable, CaptureSource source = {}) -> decltype(auto) { // We apply the environment on here
#if defined(ILIAS_CORO_TRACE)
        // TRACING: Update the current await point's line number
        // Our frame should be the top frame of the context
        // co_await sth; 
        auto frame = mContext->topFrame();
        ILIAS_ASSERT(frame); // Already push on init
        frame->setLine(source.toLocation().line());
        frame->setMessage({}); // Clear it
#endif // defined(ILIAS_CORO_TRACE)
        if constexpr (requires { awaitable.setContext(*mContext, source); }) { // It support setContext & with source
            awaitable.setContext(*mContext, source);
        }
        if constexpr (requires { awaitable.setContext(*mContext); }) { // It support setContext
            static_cast<void>(source);
            awaitable.setContext(*mContext);
        }
        return std::forward<T>(awaitable);
    }

    template <IntoRawAwaitable T>
    auto await_transform(T &&object, CaptureSource source = {}) {
        return await_transform(IntoRawAwaitableTraits<T>::into(std::forward<T>(object)), source);
    }

    // Our runtime interface
    auto takeException() noexcept {
        return std::exchange(mException, nullptr);
    }

    auto rethrowIfNeeded() {
        if (mException) {
            std::rethrow_exception(takeException());
        }
    }

    // Doing sth before the coroutine starts
    auto init() noexcept -> void {
        ILIAS_ASSERT_MSG(mContext, "Coroutine context must be set before coroutine starts");
        // TRACING: Push the frame, we are start now
        mContext->pushFrame(mCreation);
    }

    // Memory pool for coroutines (maybe.)
    auto operator new(size_t n) -> void * {
        return allocate(n);
    }

    auto operator delete(void *ptr, size_t n) noexcept -> void {
        return deallocate(ptr, n);
    }
private:
    CoroContext       *mContext = nullptr;
    std::exception_ptr mException = nullptr;
    void             (*mCompletionHandler)(CoroContext &) = nullptr; // Called when coroutine is completed, stopped is not completed
protected: // protected ...
    [[ILIAS_NO_UNIQUE_ADDRESS]] // The CaptureSource will be std::monostate if disabled, so add it
    CaptureSource           mCreation = {}; // The source of the coroutine creation
    std::coroutine_handle<> mPrevAwaiting = std::noop_coroutine(); // write by Generator :(
friend class CoroHandle;
};

// The common part handle of all stackless coroutines
class CoroHandle {
public:
    template <typename T> requires (std::is_base_of_v<CoroPromise, T>)
    CoroHandle(std::coroutine_handle<T> handle) noexcept : mHandle(handle), mPromise(&handle.promise()) {}
    CoroHandle(const CoroHandle &) noexcept = default;
    CoroHandle(std::nullptr_t) noexcept {};
    CoroHandle() noexcept = default;

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
        return executor().schedule(mHandle);
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

    // Allow comparison
    auto operator <=>(const CoroHandle &other) const noexcept = default;

    // Check if the handle is valid
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    std::coroutine_handle<> mHandle;
    CoroPromise            *mPromise = nullptr;
friend class std::hash<CoroHandle>;
};

} // namespace runtime

// Some builtin function for access the environment
namespace this_coro {

using runtime::CoroContext;
using runtime::CoroHandle;
using runtime::StackFrame;
using runtime::StopToken;
using runtime::Executor;

struct AwaiterBase {
    auto await_ready() noexcept { return true; }
    auto await_suspend(CoroHandle) noexcept {}
    auto setContext(CoroContext &ctxt) noexcept { mCtxt = &ctxt; }

    CoroContext *mCtxt = nullptr;
};

// Get the stop token from the current coroutine ctxt
[[nodiscard]] 
inline auto stopToken() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept -> StopToken {
            return mCtxt->stopSource().get_token();
        }
    };

    return Awaiter {};
}

// Check current coroutine is requested to stop
[[nodiscard]] 
inline auto isStopRequested() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept -> bool {
            return mCtxt->stopSource().stop_requested();
        }
    };

    return Awaiter {};
}

// Get the executor
[[nodiscard]] 
inline auto executor() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept -> Executor & {
            return mCtxt->executor();
        }
    };

    return Awaiter {};
}

// Try set the context stopped, it only work when the stop was requested
[[nodiscard]] 
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
[[nodiscard]] 
inline auto yield() noexcept {
    struct Awaiter {
        auto await_ready() noexcept { return false; }
        auto await_suspend(CoroHandle h) noexcept { h.schedule(); }
        auto await_resume() noexcept {}
    };

    return Awaiter {};
}

// Get the current coroutine context and process it to an callback
template <typename Fn> requires (std::invocable<Fn, CoroContext &>)
[[nodiscard]]
inline auto withContext(Fn fn) noexcept {
    struct Awaiter : AwaiterBase {
        Awaiter(Fn fn) : mFn(std::move(fn)) {}

        auto await_resume() -> decltype(auto) {
            return mFn(*mCtxt);
        }

        Fn mFn;
    };
    
    return Awaiter { std::move(fn) };
}

// Get the current virtual callstack (empty on disabled)
[[nodiscard]]
inline auto stacktrace() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept {
            return mCtxt->stacktrace();
        }
    };

    return Awaiter {};
}

} // namespace this_coro

ILIAS_NS_END

// Interop with std...
template <>
struct std::hash<ILIAS_NAMESPACE::runtime::CoroHandle> {
    auto operator()(const ILIAS_NAMESPACE::runtime::CoroHandle &h) const noexcept -> std::size_t {
        return std::hash<std::coroutine_handle<> >()(h.mHandle);
    }
};