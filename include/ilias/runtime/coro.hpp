// INTERNAL !!!
#pragma once
#include <ilias/runtime/exception.hpp> // ExceptionPtr
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/tracing.hpp> // TracingSubscriber
#include <ilias/runtime/capture.hpp> // CaptureSource, StackFrame
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/runtime/await.hpp> // Awaitable
#include <coroutine> // std::coroutine_handle<>
#include <concepts> // std::invocable
#include <utility> // std::exchange
#include <memory> // std::shared_ptr

#if defined(_MSC_VER) || defined(__clang__) || defined(__GNUC__)
    #define ILIAS_USE_CORO_ABI
#endif // _MSVC_ || __clang__ || __GNUC__

ILIAS_NS_BEGIN

// Runtime internal coroutine classes
namespace runtime {

// Memory pool for coroutines
extern auto ILIAS_API allocate(size_t n) -> void *;
extern auto ILIAS_API deallocate(void *ptr, size_t n) noexcept -> void;

// Helper class to switch between coroutines
class SwitchCoroutine {
public:
    constexpr SwitchCoroutine(std::coroutine_handle<> handle) : mHandle(handle) {}

    constexpr auto await_ready() noexcept { return false; }
    constexpr auto await_suspend(std::coroutine_handle<>) noexcept { return mHandle; }
    constexpr auto await_resume() noexcept {}
private:
    std::coroutine_handle<> mHandle;
};

// See https://devblogs.microsoft.com/oldnewthing/20220103-00/?p=106109
// HACK: Use this to optimize the size of the coroutine handle
class FrameABI {
public:
    void (*resume)(FrameABI *) = nullptr;
    void (*destroy)(FrameABI *) = nullptr;
    // std::byte promise [];

    // Extract the promise from the frame
    template <typename T>
    auto promise() noexcept -> T & {
        auto *ptr = reinterpret_cast<std::byte *>(this) + sizeof(FrameABI);
        return *reinterpret_cast<T *>(ptr);
    }

    // Get the frame from the coroutine handle
    static auto from(std::coroutine_handle<> handle) -> FrameABI * {
        return reinterpret_cast<FrameABI *>(handle.address());
    }
};

// MARK: CoroContext
// The Runtime environment for coroutines.
//
// Cancellation invariants:
// - stop_requested and stopped are different states. stop() only requests
//   cooperative cancellation through the context's StopSource; it does not
//   complete or destroy the coroutine by itself.
// - A coroutine enters the stopped state only when an awaiter observes the
//   request while the coroutine is suspended and calls CoroHandle::setStopped().
// - Once mStopped becomes true, the coroutine must not be resumed or scheduled
//   again. The stopped handler is the single handoff point to the owner/awaiter.
// - Stopped is not the same as completed: final_suspend and the completion
//   handler are used for normal/exceptional completion, while mStoppedHandler is
//   used for cooperative cancellation.
// - Contexts built with std::nostopstate are intentionally not cancellable.
// - Contexts should be Pinned to memory before any coroutine runs
class CoroContext {
public:
    CoroContext() = default;
    CoroContext(CoroContext &&) = default;
    CoroContext(const CoroContext &) = delete;
    CoroContext(std::nostopstate_t) : mStopSource(std::nostopstate) {}

    // Request to stop the coroutine
    ILIAS_API
    auto stop() noexcept -> bool;

    // Set the coroutine to stopped state
    // See more in CoroHandle::setStopped()
    ILIAS_API
    auto setStopped() noexcept -> void;

    // Check if the coroutine is stopped
    auto isStopped() const noexcept -> bool {
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

    auto setExecutor(Executor &executor) noexcept -> void {
        mExecutor = &executor;
    }

    auto setStoppedHandler(void (*handler)(CoroContext &)) noexcept -> void {
        mStoppedHandler = handler;
    }

    auto setUserdata(void *user) noexcept -> void {
        mUser = user;
    }

    // TRACING: Get the trace context
    auto tracing() const noexcept -> const TraceContext & {
        return mTraceContext;
    }

    auto tracing() noexcept -> TraceContext & {
        return mTraceContext;
    }

    // Other operator
    auto operator =(CoroContext &&) -> CoroContext & = default;
    auto operator =(const CoroContext &) -> CoroContext & = delete;
private:
    StopSource    mStopSource;                               // Used to request cooperative cancellation
    Executor     *mExecutor = nullptr;
    void        (*mStoppedHandler)(CoroContext &) = nullptr; // Called when coroutine is stopped
    void         *mUser = nullptr;                           // The user data, useful in the callback
    bool          mStopped = false;                          // The coroutine is actually stopped
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    TraceContext  mTraceContext;                             // The context used for tracing
friend class CoroPromise;
friend class CoroHandle;
};

// MARK: CoroPromise
// The common part of all stackless coroutines
class CoroPromise {
public:
    CoroPromise(const CoroPromise &) = delete;
    CoroPromise() = default;

    // std coroutine interface
    auto initial_suspend() noexcept {
        struct Awaiter {
            constexpr
            auto await_ready() noexcept { return false; }
            auto await_suspend(std::coroutine_handle<>) noexcept {}
            auto await_resume() noexcept { self.init(); }
            CoroPromise &self;
        };
        return Awaiter {*this};
    }

    auto final_suspend() noexcept {
        struct Awaiter {
            constexpr
            auto await_ready() noexcept { return false; }
            auto await_suspend(std::coroutine_handle<>) noexcept { return self.final(); }
            [[noreturn]]
            auto await_resume() noexcept { // UNREACHABLE here, we can't resume an done coroutine
                ILIAS_UNREACHABLE(); // LCOV_EXCL_LINE
            }
            CoroPromise &self;
        };
        return Awaiter {*this};
    }

    auto unhandled_exception() noexcept -> void { 
        mException = ExceptionPtr::currentException();
    }

    // co_await for raw awaitable (like std::suspend_never, Task<T>, etc)
    template <RawAwaitable T, bool Forward = true>
    auto await_transform(T &&awaitable, [[maybe_unused]] CaptureSource source = {}) -> decltype(auto) { // We apply the environment on here
#if defined(ILIAS_CORO_TRACE)
        // TRACING: Update the current await point's line number
        // Our frame should be the top frame of the context
        // co_await sth; 
        auto frame = mContext->tracing().topFrame();
        ILIAS_ASSERT(frame); // Already push on init
        frame->setLine(source.toLocation().line());
        frame->setMessage({}); // Clear it
#endif // defined(ILIAS_CORO_TRACE)
        if constexpr (requires { awaitable.setContext(*mContext, source); }) { // It support setContext & with source
            awaitable.setContext(*mContext, source);
        }
        else if constexpr (requires { awaitable.setContext(*mContext); }) { // It support setContext
            awaitable.setContext(*mContext);
        }
#if defined(ILIAS_CORO_TRACE)
        if constexpr (requires { typename T::SkipTracing; }) {
            return std::forward<T>(awaitable);
        }
        else {
            static_assert(Forward || std::move_constructible<std::decay_t<T> >, "Awaitable must be move_constructible, it will be moved to the awaiter");
            return TracingAwaitable<T, Forward> { std::forward<T>(awaitable), mContext->tracing()}; // Wrap it with tracing
        }
#else
        return std::forward<T>(awaitable);
#endif // defined(ILIAS_CORO_TRACE)
    }

    // co_await for can be converted to raw awaitable
    template <IntoRawAwaitable T>
    auto await_transform(T &&object, CaptureSource source = {}) {
        auto awaitable = IntoRawAwaitableTrait<T>::into(std::forward<T>(object));
        return await_transform<decltype(awaitable), false>(std::move(awaitable), source); // Move into inner if necessary
    }

    // Our runtime interface
    auto takeException() noexcept -> ExceptionPtr {
        return std::exchange(mException, nullptr);
    }

    auto rethrowIfAny() -> void {
        return takeException().rethrowIfAny();
    }

    // Doing sth before the coroutine starts
    auto init() noexcept -> void {
        ILIAS_ASSERT(mContext, "Coroutine context must be set before coroutine starts");
#if defined(ILIAS_CORO_TRACE)
        // TRACING: Push the frame, we are start now
        mContext->tracing().resume();
        mContext->tracing().pushFrame(mCreation);
#endif // defined(ILIAS_CORO_TRACE)
    }

    // Doing sth after the coroutine done
    auto final() noexcept -> std::coroutine_handle<> {
        if (mCompletionHandler) {
            mCompletionHandler(*mContext);
        }
#if defined(ILIAS_CORO_TRACE)
        // TRACING: Cleanup the frame belong to us
        mContext->tracing().popFrame();
#endif // defined(ILIAS_CORO_TRACE)
        return mPrevAwaiting;
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
    [[ILIAS_NO_UNIQUE_ADDRESS]] // The ExceptionPtr will be empty class if disabled
    ExceptionPtr       mException = nullptr;
    void             (*mCompletionHandler)(CoroContext &) = nullptr; // Called when coroutine is completed, stopped is not completed for promise
protected: // protected ...
    [[ILIAS_NO_UNIQUE_ADDRESS]] // The CaptureSource will be std::monostate if disabled, so add it
    CaptureSource           mCreation = {}; // The source of the coroutine creation
    std::coroutine_handle<> mPrevAwaiting = std::noop_coroutine(); // write by Generator :(
friend class CoroHandle;
};

// MARK: CoroHandle
// The common part handle of all stackless coroutines
class CoroHandle {
public:
    template <typename T> requires (std::is_base_of_v<CoroPromise, T>)
    CoroHandle(std::coroutine_handle<T> handle) noexcept : mHandle(handle) {
#if defined(ILIAS_USE_CORO_ABI)
        ILIAS_ASSERT(&promise() == &handle.promise(), "Coroutine frame abi mismatch");
#else
        mPromise = &handle.promise(); 
#endif // defined(ILIAS_USE_CORO_ABI)
    }
    CoroHandle(std::nullptr_t) noexcept {}
    CoroHandle() noexcept = default;

    // std coroutine interface
    auto done() const noexcept -> bool {
        return mHandle.done();
    }

    auto resume() const noexcept -> void {
        ILIAS_ASSERT(!context().isStopped(), "Cannot resume a stopped coroutine");
        return mHandle.resume();
    }

    auto destroy() const noexcept -> void {
        return mHandle.destroy();
    }

    // Get the promise of the coroutine
    template <typename T = CoroPromise>
    auto promise() const noexcept -> T & {
        ILIAS_ASSERT(mHandle, "Can't get promise from null handle");
#if defined(ILIAS_USE_CORO_ABI)
        auto *frame = FrameABI::from(mHandle);
        return frame->promise<T>();
#else
        return static_cast<T &>(*mPromise);
#endif // defined(ILIAS_USE_CORO_ABI)
    }

    // Our runtime interface
    auto context() const noexcept -> CoroContext & {
        ILIAS_ASSERT(mHandle, "Can't get context from null handle");
        return *(promise().mContext); // Context must be set before coroutine starts
    }

    // Get the executor of the coroutine
    auto executor() const noexcept -> Executor & {
        return *context().mExecutor; // Executor must not be null
    }

    // Set the context of the coroutine
    auto setContext(CoroContext &ctxt) const noexcept -> void {
        promise().mContext = &ctxt;
    }

    // Transition the context from stop-requested to stopped.
    // This may only be called once, after a stop request has been observed by a
    // suspended coroutine. The stopped handler owns the follow-up action, such
    // as notifying a parent awaiter or releasing a spawned task.
    auto setStopped() const noexcept -> void {
        auto &ctxt = context();
        ILIAS_ASSERT(ctxt.mStoppedHandler, "Stopped handler must be set, double call on CoroHandle::setStopped() ?");
        ILIAS_ASSERT(ctxt.mStopSource.stop_possible(), "Stop source must be possible to stop, invalid state ?");
        ILIAS_ASSERT(ctxt.mStopSource.stop_requested(), "Stop source must be requested, invalid state ?");
        ILIAS_ASSERT(!ctxt.mStopped, "Cannot set stopped twice");
        return ctxt.setStopped();
    }

    // Set the completion handler, it will be called when coroutine is completed, stopped is not completed
    auto setCompletionHandler(void (*handler)(CoroContext &)) const noexcept -> void {
        promise().mCompletionHandler = handler;
    }

    // Set the previous awaiting coroutine, when the coroutine is completed, it will resume the previous awaiting coroutine
    auto setPrevAwaiting(CoroHandle h) const noexcept -> void {
        promise().mPrevAwaiting = h.mHandle;
    }

    // Resume in the executor
    auto schedule() const noexcept -> void {
        ILIAS_ASSERT(!context().isStopped(), "Cannot schedule a stopped coroutine");
        return executor().schedule(mHandle);
    }

    // Get the stop source from the environment
    auto stopToken() const noexcept -> StopToken {
        return context().mStopSource.get_token();
    }

    auto takeException() const noexcept -> ExceptionPtr {
        return promise().takeException();
    }

    auto isStopRequested() const noexcept -> bool {
        return context().mStopSource.stop_requested();
    }

    // Get the std coroutine handle
    auto toStd() const noexcept -> std::coroutine_handle<> {
        return mHandle;
    }

    // Allow comparison
    auto operator <=>(const CoroHandle &other) const noexcept = default;

    // Check if the handle is valid
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    std::coroutine_handle<> mHandle; // The std coroutine handle
#if !defined(ILIAS_USE_CORO_ABI)
    CoroPromise            *mPromise = nullptr; // The promise of the coroutine
#endif // defined(ILIAS_USE_CORO_ABI)
};

} // namespace runtime

// MARK: This Coro
// Some builtin function for access the environment
namespace this_coro {

using runtime::CoroContext;
using runtime::CoroHandle;
using runtime::StackFrame;
using runtime::StopToken;
using runtime::Executor;

struct AwaiterBase {
    using SkipTracing = void;

    constexpr
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
    struct Awaiter {       
        auto setContext(CoroContext &ctxt) noexcept { 
            auto &source = ctxt.stopSource();
            if (source.stop_possible()) {
                mStopped = source.stop_requested(); 
            }
        }
        auto await_ready() const noexcept { // If stop requested, enter the stopped state (never resume)
            return !mStopped;
        }
        auto await_suspend(CoroHandle h) noexcept {
            h.setStopped();
        }
        auto await_resume() const noexcept {
            ILIAS_ASSUME(!mStopped, "Coro is stopped, but still resume"); // LCOV_EXCL_LINE
        }

        bool mStopped = false;
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

// Get the current virtual callstack (empty on disabled)
[[nodiscard]]
inline auto stacktrace() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept {
            return mCtxt->tracing().stacktrace();
        }
    };

    return Awaiter {};
}

// Get the current name from the coroutine context
[[nodiscard]]
inline auto name() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept {
            return mCtxt->tracing().name();
        }
    };

    return Awaiter {};
}

// Set the name to the current coroutine context
inline auto setName(std::string_view name) noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept {
            mCtxt->tracing().setName(name);
        }

        std::string_view name;
    };

    Awaiter awaiter {};
    awaiter.name = name;
    return awaiter;
}

} // namespace this_coro

ILIAS_NS_END

// Interop with std...
template <>
struct std::hash<ilias::runtime::CoroHandle> {
    auto operator()(const ilias::runtime::CoroHandle &h) const noexcept -> std::size_t {
        return std::hash<std::coroutine_handle<> >{}(h.toStd());
    }
};
