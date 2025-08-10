/**
 * @file fiber.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The stackful asymmetric coroutine, use the same model as std::coroutine
 * @version 0.1
 * @date 2025-08-08
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include <ilias/defines.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp> // runtime::CoroHandle
#include <ilias/detail/option.hpp> // Option<void>
#include <ilias/task/task.hpp> // Task<T>
#include <exception> // std::exception_ptr
#include <memory> // std::unique_ptr
#include <tuple> // std::tuple, std::apply

#if !defined(__cpp_exceptions)
    #error "Fiber requires exceptions to unwind the stack"
#endif // __cpp_exceptions

ILIAS_NS_BEGIN

namespace fiber {

// The entry point configure of the fiber
class FiberEntry {
public:
    void  (*cleanup)(void *) = nullptr; // For cleanup the args, destroy when the fiber context is destroyed
    void *(*invoke)(void *) = nullptr; // The entry point of the fiber, return the pointer of the result
    void   *args = nullptr;
    size_t  stackSize = 0; // The stack size of the fiber (0 for default)
};

// The context of the fiber (user part)
class ILIAS_API FiberContext {
public:
    // std coroutine like interface
    auto destroy() -> void;
    auto resume() -> void;
    auto done() const -> bool { return mComplete; }

    // Our interface
    // Resume the fiber in the executor
    auto schedule() -> void;

    // Get the result of the fiber
    template <typename T>
    auto value() -> T;

    // Check if the fiber is stopped, (no value produced)
    auto isStopped() const noexcept -> bool { 
        return mStopped; 
    }

    // Get the stop token
    auto stopToken() const noexcept -> runtime::StopToken {
        return mStopSource.get_token();
    }

    auto stopSource() noexcept -> runtime::StopSource & {
        return mStopSource;
    }

    auto executor() noexcept -> runtime::Executor & {
        return *mExecutor;
    }

    // Set the executor
    auto setExecutor(runtime::Executor &executor) -> void {
        mExecutor = &executor;
    }

    // Set the completion handler
    auto setCompletionHandler(void (*handler)(FiberContext *, void *), void *user) -> void {
        mCompletionHandler = handler;
        mUser = user;
    }

    // Suspend the current fiber, if current() == nullptr, abort
    static auto suspend() -> void;

    // Set Stopped in the current fiber, equal to throw FiberUnwind 
    [[noreturn]]
    static auto stopped() -> void;

    // Get the current fiber context
    static auto current() -> FiberContext *;

    // Create the fiber by given entry
    static auto create4(FiberEntry) -> FiberContext *;

    // Create the fiber by given callable
    template <typename Fn, typename ...Args>
    static auto create(Fn fn, Args ...args) -> FiberContext *;
protected: // Don't allow to directly create the context
    FiberContext() = default;
    ~FiberContext() = default;

    // State
    runtime::StopSource mStopSource;
    runtime::Executor *mExecutor = nullptr;
    bool mUnwinding = false;
    bool mComplete = false;
    bool mStopped = false;

    // Handler invoked when complete
    void (*mCompletionHandler)(FiberContext *ctxt, void *) = nullptr;
    void  *mUser = nullptr;

    // Entry
    struct {
        void  (*cleanup)(void *) = nullptr;
        void *(*invoke)(void *) = nullptr;
        void   *args = nullptr;
    } mEntry;

    // Result
    void *mValue = nullptr;
    std::exception_ptr mException;
};

// The callable of the fiber, for store the fn, args and the return value
template <typename Fn, typename ...Args>
class FiberCallable {
public:
    using T = std::invoke_result_t<Fn, Args...>;

    FiberCallable(Fn fn, Args ...args) : mFn(fn), mArgs(args...) {}

    static auto invoke(void *args) -> void * {
        auto self = static_cast<FiberCallable *>(args);
        self->mValue = makeOption([&]() {
            return std::apply(self->mFn, self->mArgs);
        });
        return &self->mValue;
    }

    static auto cleanup(void *args) noexcept -> void {
        auto self = static_cast<FiberCallable *>(args);
        delete self;
    }
private:
    Option<T> mValue; // Put the result here

    [[no_unique_address]]
    Fn mFn;
    [[no_unique_address]] // The args may empty
    std::tuple<Args...> mArgs;
};

class Deleter {
public:
    auto operator()(FiberContext *ctxt) -> void {
        ctxt->destroy();
    }
};

using FiberHandle = std::unique_ptr<FiberContext, Deleter>;

// Awaiter for fiber, provide the bridge between fiber and std::coroutine
class FiberAwaiterBase {
public:
    FiberAwaiterBase(FiberHandle handle) : mHandle(std::move(handle)) {}

    auto await_ready() const -> bool {
        mHandle->resume();
        return mHandle->done();
    }

    auto await_suspend(runtime::CoroHandle caller) -> void {
        mCaller = caller;
        mHandle->setCompletionHandler(onCompletion, this);
        mReg.register_<&FiberAwaiterBase::onStopRequested>(caller.stopToken(), this);
    }
protected:
    auto onStopRequested() -> void {
        mHandle->stopSource().request_stop(); // forward the stop request to the fiber
    }

    static auto onCompletion(FiberContext *ctxt, void *_self) -> void {
        auto self = static_cast<FiberAwaiterBase *>(_self);
        if (ctxt->isStopped()) {
            self->mCaller.setStopped(); // Forward the stop to the caller
            return;
        }
        self->mCaller.schedule();
    }

    FiberHandle mHandle;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

template <typename T>
class FiberAwaiter : public FiberAwaiterBase {
public:
    using FiberAwaiterBase::FiberAwaiterBase;

    auto await_resume() -> T {
        return mHandle->value<T>();
    }
};

template <typename T>
inline auto FiberContext::value() -> T {
    ILIAS_ASSERT_MSG(mComplete, "Fiber not complete yet");
    ILIAS_ASSERT_MSG(!mStopped, "Fiber is stopped, no value provided");
    if (mException) {
        std::rethrow_exception(mException);
    }
    auto result = static_cast<Option<T> *>(mValue);
    return unwrapOption(std::move(*result));
}

template <typename Fn, typename ...Args>
inline auto FiberContext::create(Fn fn, Args ...args) -> FiberContext * {
    using Callable = FiberCallable<Fn, Args...>;
    auto ptr = new Callable(std::forward<Fn>(fn), std::forward<Args>(args)...);
    return create4({
        .cleanup = Callable::cleanup,
        .invoke = Callable::invoke,
        .args = ptr,
    });
}

} // namespace fiber

// Operations on the current fiber
namespace this_fiber {

// Yield the current fiber, resume the fiber when the next time it is scheduled
extern auto ILIAS_API yield() -> void;

// INTERNAL!!!, wait the stackless CoroHandle to done or stopped
extern auto ILIAS_API await4(runtime::CoroHandle) -> void;

template <typename T>
inline auto await(Task<T> task) -> T {
    auto handle = task::TaskHandle<T>(task._handle());
    await4(handle);
    return handle.value();
}

template <Awaitable T>
inline auto await(T awaitable) -> AwaitableResult<T> {
    return await(toTask(std::move(awaitable)));
}

} // namespace this_fiber

/**
 * @brief The Fiber class, stackful coroutine, hold the context of the coroutine
 * 
 * @tparam T 
 */
template <typename T>
class Fiber {
public:
    Fiber() = default;
    Fiber(const Fiber &) = delete;
    Fiber(Fiber &&) noexcept = default;

    /**
     * @brief Construct a new Fiber object
     * 
     * @tparam Fn 
     * @tparam Args 
     * @param fn The function to run in the fiber
     * @param args The arguments to the function
     */
    template <typename Fn, typename ...Args>
        requires (std::invocable<Fn, Args...>)
    explicit Fiber(Fn fn, Args ...args) : 
        mHandle(fiber::FiberContext::create(std::forward<Fn>(fn), std::forward<Args>(args)...)) 
    {

    }

    /**
     * @brief Blocking wait for the fiber to complete
     * 
     * @return T 
     */
    auto wait() && -> T {
        auto executor = runtime::Executor::currentThread();
        auto handle = std::move(mHandle);
        handle->setExecutor(*executor);
        handle->resume();
        if (!handle->done()) {
            runtime::StopSource source;
            handle->setCompletionHandler([](fiber::FiberContext *ctxt, void *user) {
                static_cast<runtime::StopSource *>(user)->request_stop();
            }, &source);
            executor->run(source.get_token());
        }
        return handle->value<T>();
    }

    // Set the context of the fiber, call on await_transform
    auto setContext(runtime::CoroContext &ctxt) -> void {
        mHandle->setExecutor(ctxt.executor());
    }

    auto operator =(const Fiber &) -> Fiber & = delete;
    auto operator =(Fiber &&) -> Fiber & = default;
    auto operator co_await() && -> fiber::FiberAwaiter<T> {
        return {std::move(mHandle)};
    }

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    fiber::FiberHandle mHandle;
};

template <typename Fn, typename ...Args>
Fiber(Fn, Args ...) -> Fiber<std::invoke_result_t<Fn, Args...> >;

/**
 * @brief The special exception for fiber unwind
 * 
 */
class FiberUnwind {};

ILIAS_NS_END