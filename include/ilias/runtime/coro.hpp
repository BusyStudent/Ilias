// INTERNAL !!!
#pragma once
#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/token.hpp>
#include <functional>
#include <stop_token>
#include <coroutine>
#include <exception>

ILIAS_NS_BEGIN

// Runtime internal coroutine classes
namespace runtime {

class SwitchCoroutine {
public:
    SwitchCoroutine(std::coroutine_handle<> handle) : mHandle(handle) { }

    auto await_ready() noexcept { return false; }
    auto await_suspend(std::coroutine_handle<>) noexcept { return mHandle; }
    auto await_resume() noexcept { }
private:
    std::coroutine_handle<> mHandle;
};

// The Runtime environment for coroutines
class CoroContext {
public:
    StopSource  stopSource;                               // Use this to try to stop the coroutine
    Executor   *executor = nullptr;
    void      (*stoppedHandler)(CoroContext &) = nullptr; // Called when coroutine is stopped

    auto stop() {
        stopSource.request_stop();
    }
    auto isStopped() const {
        return mStopped;
    }
private:
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

    template <typename T>
    auto await_transform(T &&awaitable) -> decltype(auto) { // We apply the environment on here
        if constexpr (requires { awaitable.setContext(*mContext); }) { // It support it
            awaitable.setContext(*mContext);
        }
        return std::forward<T>(awaitable);
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
    std::coroutine_handle<> mPrevAwaiting = std::noop_coroutine();
    void (*mCompletionHandler)(CoroContext &) = nullptr; // Called when coroutine is completed, stopped is not completed
friend class CoroHandle;
};

// The common part handle of all coroutines
class CoroHandle {
public:
    template <typename T> requires (std::is_base_of_v<CoroPromise, T>)
    CoroHandle(std::coroutine_handle<T> handle) : mHandle(handle), mPromise(&handle.promise()) {}
    CoroHandle(const CoroHandle &) = default;
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
        return *context().executor; // Executor must not be null
    }

    auto setContext(CoroContext &ctxt) noexcept {
        mPromise->mContext = &ctxt;
    }

    // Tell the context, are we stopped now, it only can called when the stop was requested and coroutine is suspended
    auto setStopped() noexcept {
        auto &ctxt = context();
        ILIAS_ASSERT_MSG(ctxt.stoppedHandler, "Stopped handler must be set, double call on CoroHandle::setStopped() ?");
        ILIAS_ASSERT_MSG(ctxt.stopSource.stop_requested(), "Stop source must be requested, invalid state ?");
        ctxt.stoppedHandler(ctxt); // Call the stopped handler, we are stopped
        ctxt.stoppedHandler = nullptr; // Mark it as called
        ctxt.mStopped = true;
    }

    // Set the completion handler, it will be called when coroutine is completed, stopped is not completed
    auto setCompletionHandler(void (*handler)(CoroContext &)) noexcept {
        mPromise->mCompletionHandler = handler;
    }

    // Set the previous awaiting coroutine, when the coroutine is completed, it will resume the previous awaiting coroutine
    auto setPrevAwaiting(CoroHandle h) noexcept {
        mPromise->mPrevAwaiting = h.mHandle;
    }

    // Resume in the executor
    auto schedule() const noexcept {
        ILIAS_ASSERT_MSG(!context().isStopped(), "Cannot schedule a stopped coroutine");
        return context().executor->post(scheduleImpl, mHandle.address());
    }

    auto stopToken() const noexcept {
        return context().stopSource.get_token();
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

ILIAS_NS_END

// Interop with std...
template <>
struct std::hash<ILIAS_NAMESPACE::runtime::CoroHandle> {
    auto operator()(const ILIAS_NAMESPACE::runtime::CoroHandle &h) const noexcept -> std::size_t {
        return std::hash<std::coroutine_handle<> >()(h.mHandle);
    }
};