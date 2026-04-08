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
extern auto ILIAS_API allocationSize() noexcept -> size_t; // Get previous allocation size (useable when trace)

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

// MARK: CoroContext
// The Runtime environment for coroutines
class CoroContext {
public:
    CoroContext() = default;
    CoroContext(CoroContext &&) = default;
    CoroContext(std::nostopstate_t) : mStopSource(std::nostopstate) {}

    // Request to stop the coroutine
    auto stop() noexcept -> bool {
        return mStopSource.request_stop();
    }

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

    // MARK: Tracing
#if defined(ILIAS_CORO_TRACE)
    // TRACING: Set the parent of the ctxt
    auto setParent(CoroContext &parent) noexcept -> void {
        if (!parent.mParent) { // The parent is the root
            mRoot = &parent;
        }
        else {
            mRoot = parent.mRoot; // Has parent, used the cache
        }
        mParent = &parent;
    }

    // TRACING: Set the name of the ctxt
    auto setName(std::string_view name) noexcept -> void {
        mName.assign(name);
    }

    // TRACING: Set the extra data of the ctxt
    auto setExtraData(std::shared_ptr<void> data) noexcept -> void {
        mExtra = data;
    }

    // TRACING: Push the frame to the stack, return the index of the frame
    template <typename ...Args>
    auto pushFrame(Args &&...args) noexcept {
        mFrames.emplace_back(std::forward<Args>(args)...);
        return mFrames.size() - 1;
    }

    // TRACING: Pop the frame from the stack
    auto popFrame() noexcept -> void {
        ILIAS_ASSERT(!mFrames.empty());
        mFrames.pop_back();
    }

    // TRACING: Get the top frame of the ctxt (return pointer, nullptr on empty)
    auto topFrame() noexcept {
        return mFrames.empty() ? nullptr : &mFrames.back();
    }

    auto topFrame() const noexcept {
        return mFrames.empty() ? nullptr : &mFrames.back();
    }

    // TRACING: Get the stacktrace of the ctxt
    auto stacktrace() const noexcept {
        auto vec = std::vector<StackFrame> {};
        for (auto cur = this; cur != nullptr; cur = cur->mParent) {
            vec.insert(vec.end(), cur->mFrames.rbegin(), cur->mFrames.rend());
        }
        return Stacktrace { std::move(vec) };
    }

    // TRACING: Get the parent of the ctxt
    auto parent() const noexcept {
        return mParent;
    }

    // TRACING: Get the root of the ctxt
    auto root() const noexcept {
        return mRoot;
    }

    // TRACING: Get the debug name of the ctxt
    auto name() const noexcept {
        return std::string_view {mName};
    }

    // TRACING: Get the extra data of the ctxt
    template <typename T>
    auto extraData() const noexcept -> T * {
        return static_cast<T *>(mExtra.get());
    }
#else // Disabled
    auto setParent(CoroContext &) noexcept {}
    auto setName(std::string_view) noexcept {}
    auto pushFrame(auto ...) noexcept { return 0; }
    auto popFrame() noexcept {}
    auto topFrame() const noexcept { return static_cast<StackFrame *>(nullptr); }
    auto stacktrace() const noexcept { return Stacktrace {}; }
    auto parent() const noexcept { return static_cast<CoroContext *>(nullptr); }
    auto root() const noexcept { return static_cast<CoroContext *>(nullptr); }
    auto name() const noexcept { return std::string_view {}; }
#endif // defined(ILIAS_CORO_TRACE)

    // Other operator
    auto operator =(CoroContext &&) -> CoroContext & = default;

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
    bool          mStopped = false;                          // The coroutine is actually stopped
#if defined(ILIAS_CORO_TRACE)
    bool          mSuspended = true;                         // The coroutine is suspended
    CoroContext  *mParent = nullptr;                         // Use for stacktrace to dump the whole stack
    CoroContext  *mRoot = nullptr;                           // The root context of the coroutine (spawn or blocking wait), used for tracing
    std::string   mName;                                     // The name of the coroutine, used for tracing
    size_t        mStackSize = 0;                            // The size of the stack, used for tracing
    std::shared_ptr<void>   mExtra;                          // The extra data for tracing
    std::vector<StackFrame> mFrames;                         // The frames of the coroutine,
#endif // defined(ILIAS_CORO_TRACE)
template <typename T, bool Forward>
friend class TracingAwaitable;
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

    auto unhandled_exception() noexcept { 
        mException = ExceptionPtr::currentException();
    }

    template <RawAwaitable T, bool Forward = true>
    auto await_transform(T &&awaitable, [[maybe_unused]] CaptureSource source = {}) -> decltype(auto) { // We apply the environment on here
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
        else if constexpr (requires { awaitable.setContext(*mContext); }) { // It support setContext
            awaitable.setContext(*mContext);
        }
#if defined(ILIAS_CORO_TRACE)
        if constexpr (requires { typename T::SkipTracing; }) {
            return std::forward<T>(awaitable);
        }
        else {
            static_assert(Forward || std::move_constructible<std::decay_t<T> >, "Awaitable must be move_constructible, it will be moved to the awaiter");
            return TracingAwaitable<T, Forward> { std::forward<T>(awaitable), mContext}; // Wrap it with tracing
        }
#else
        return std::forward<T>(awaitable);
#endif // defined(ILIAS_CORO_TRACE)
    }

    template <IntoRawAwaitable T>
    auto await_transform(T &&object, CaptureSource source = {}) {
        auto awaitable = IntoRawAwaitableTraits<T>::into(std::forward<T>(object));
        return await_transform<decltype(awaitable), false>(std::move(awaitable), source); // Move into inner if necessary
    }

    // Our runtime interface
    auto takeException() noexcept {
        return std::exchange(mException, nullptr);
    }

    auto rethrowIfAny() {
        return takeException().rethrowIfAny();
    }

    // Doing sth before the coroutine starts
    auto init() noexcept -> void {
        ILIAS_ASSERT(mContext, "Coroutine context must be set before coroutine starts");
        ILIAS_ASSERT(!mDone, "Coroutine already done, memory corruption ???");
#if defined(ILIAS_CORO_TRACE)
        // TRACING: Push the frame, we are start now
        if (mContext->mSuspended) {
            mContext->mSuspended = false;
            tracing::resume(*mContext);
        }
        mContext->pushFrame(mCreation);
#endif // defined(ILIAS_CORO_TRACE)
    }

    // Doing sth after the coroutine done
    auto final() noexcept -> std::coroutine_handle<> {
        ILIAS_ASSERT(!mDone, "Coroutine already done, internal BUG!!!");
        mDone = true;
        if (mCompletionHandler) {
            mCompletionHandler(*mContext);
        }
#if defined(ILIAS_CORO_TRACE)
        // TRACING: Cleanup the frame belong to us
        mContext->popFrame();
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
    bool               mDone = false; // Logical done, used for ILIAS_CO_TRY
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
    auto done() const noexcept {
        return mHandle.done() || promise().mDone;
    }

    auto resume() const noexcept {
        ILIAS_ASSERT(!context().isStopped(), "Cannot resume a stopped coroutine");
        return mHandle.resume();
    }

    auto destroy() const noexcept {
        return mHandle.destroy();
    }

    // Get the promise of the coroutine
    template <typename T = CoroPromise>
    auto promise() const noexcept -> T & {
        ILIAS_ASSERT(mHandle, "Can't get promise from null handle");
#if defined(ILIAS_USE_CORO_ABI)
        auto frame = reinterpret_cast<FrameABI *>(mHandle.address());
        auto promise = reinterpret_cast<T *>(frame->promise);
        return *promise;
#else
        return static_cast<T &>(*mPromise);
#endif // defined(ILIAS_USE_CORO_ABI)
    }

    // Our runtime interface
    auto context() const noexcept -> CoroContext & {
        ILIAS_ASSERT(mHandle, "Can't get context from null handle");
        return *(promise().mContext); // Context must be set before coroutine starts
    }

    auto executor() const noexcept -> Executor & {
        return *context().mExecutor; // Executor must not be null
    }

    auto setContext(CoroContext &ctxt) const noexcept {
        promise().mContext = &ctxt;
    }

    // Tell the context, are we stopped now, it only can called when the stop was requested and coroutine is suspended
    auto setStopped() const noexcept {
        auto &ctxt = context();
        ILIAS_ASSERT(ctxt.mStoppedHandler, "Stopped handler must be set, double call on CoroHandle::setStopped() ?");
        ILIAS_ASSERT(ctxt.mStopSource.stop_possible(), "Stop source must be possible to stop, invalid state ?");
        ILIAS_ASSERT(ctxt.mStopSource.stop_requested(), "Stop source must be requested, invalid state ?");
        ctxt.mStopped = true;
        ctxt.mStoppedHandler(ctxt); // Call the stopped handler, we are stopped
        ctxt.mStoppedHandler = nullptr; // Mark it as called
    }

    // Set the completion handler, it will be called when coroutine is completed, stopped is not completed
    auto setCompletionHandler(void (*handler)(CoroContext &)) const noexcept {
        promise().mCompletionHandler = handler;
    }

    // Set the previous awaiting coroutine, when the coroutine is completed, it will resume the previous awaiting coroutine
    auto setPrevAwaiting(CoroHandle h) const noexcept {
        promise().mPrevAwaiting = h.mHandle;
    }

    // Resume in the executor
    auto schedule() const noexcept {
        ILIAS_ASSERT(!context().isStopped(), "Cannot schedule a stopped coroutine");
        return executor().schedule(mHandle);
    }

    auto stopToken() const noexcept {
        return context().mStopSource.get_token();
    }

    auto takeException() const noexcept {
        return promise().takeException();
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
    std::coroutine_handle<> mHandle; // The std coroutine handle
#if !defined(ILIAS_USE_CORO_ABI)
    CoroPromise            *mPromise = nullptr; // The promise of the coroutine
#else
    // See https://devblogs.microsoft.com/oldnewthing/20220103-00/?p=106109
    // HACK: Use this to optimize the coroutine handle
    struct FrameABI {
        void (*resume)(FrameABI *);
        void (*destroy)(FrameABI *);
        std::byte promise[];
    };
#endif // defined(ILIAS_USE_CORO_ABI)
};

#if defined(ILIAS_CORO_TRACE)
// MARK: TracingAwaitable
// Add hooks to an awaitable, used for tracing
template <typename T, bool Forward>
class TracingAwaitable {
public:
    template <typename U>
    using DecayIf     = std::conditional_t<Forward, U, std::decay_t<U> >;
    using Awaitable   = DecayIf<T>;
    using Awaiter     = DecayIf<decltype(toAwaiter(std::declval<T>()))>;

    // Move version, store it by value
    TracingAwaitable(T awaitable, CoroContext *ctxt) requires(!Forward) : 
        mAwaitable(std::move(awaitable)), 
        mAwaiter(toAwaiter(std::move(mAwaitable))),
        mCtxt(ctxt) {}

    // Forward version, just store the reference
    TracingAwaitable(T awaitable, CoroContext *ctxt) requires(Forward) :
        mAwaitable(std::forward<T>(awaitable)),
        mAwaiter(toAwaiter(std::forward<T>(mAwaitable))),
        mCtxt(ctxt) {}

    // MUST NRVO, Pin the awaitable, avoid the awaiter implementation need an stable awaitable address, it will cause dangling if move
    TracingAwaitable(const TracingAwaitable &) = delete;

    // Hooks
    auto await_ready() noexcept(noexcept(mAwaiter.await_ready())) { 
        return mAwaiter.await_ready(); 
    }

    template <typename U>
    auto await_suspend(std::coroutine_handle<U> handle) noexcept(noexcept(mAwaiter.await_suspend(handle))) {
        using Ret = decltype(mAwaiter.await_suspend(handle));
        if constexpr (std::is_same_v<Ret, void>) {
            mAwaiter.await_suspend(handle);
            suspend();
            return;
        }
        else if constexpr (std::convertible_to<Ret, bool>) { // Return bool
            auto ret = mAwaiter.await_suspend(handle);
            if (ret) { // true on actually suspend
                suspend();
            }
            return ret;
        }
        else { // std::coroutine_handle<>
            auto ret = mAwaiter.await_suspend(handle);
            suspend();
            return ret;
        }
    }

    auto await_resume() noexcept(noexcept(mAwaiter.await_resume())) -> decltype(auto) { 
        if (mCtxt->mSuspended) { // Notify the tracing, we actually resume
            mCtxt->mSuspended = false;
            tracing::resume(*mCtxt);
        }
        return mAwaiter.await_resume();
    }
private:
    auto suspend() noexcept -> void {
        if (!mCtxt->mSuspended) { // Notify the tracing, we actually suspend
            mCtxt->mSuspended = true;
            tracing::suspend(*mCtxt);
        }
    }

    Awaitable mAwaitable;
    Awaiter   mAwaiter;
    CoroContext *mCtxt;
};
#endif // defined(ILIAS_CORO_TRACE)

} // namespace runtime

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
            mStopped = ctxt.stopSource().stop_requested(); 
        }
        auto await_ready() noexcept { // If stop requested, enter the stopped state (never resume)
            return !mStopped;
        }
        auto await_suspend(CoroHandle h) noexcept {
            h.setStopped();
        }
        auto await_resume() const noexcept {
            // LCOV_EXCL_START
            if (mStopped) {
                ILIAS_UNREACHABLE();
            }
            // LCOV_EXCL_STOP
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
            return mCtxt->stacktrace();
        }
    };

    return Awaiter {};
}

// Get the current name from the coroutine context
[[nodiscard]]
inline auto name() noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept {
            return mCtxt->name();
        }
    };

    return Awaiter {};
}

// Set the name to the current coroutine context
inline auto setName(std::string_view name) noexcept {
    struct Awaiter : AwaiterBase {
        auto await_resume() noexcept {
            mCtxt->setName(name);
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