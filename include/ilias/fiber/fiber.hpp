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

#include <ilias/runtime/capture.hpp> // runtime::CaptureSource
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp> // runtime::CoroHandle
#include <ilias/detail/option.hpp> // Option<void>
#include <ilias/task/task.hpp> // Task<T>
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
    // Resume the fiber, return bool for whether the fiber is done
    auto resume() -> bool;

    // Blocking wait the fiber to be done
    auto wait(runtime::CaptureSource where) -> void;

    // Destroy the fiber
    auto destroy() -> void;

    // Get the result of the fiber
    template <typename T>
    auto value() -> T;

    // Set the executor
    auto setExecutor(runtime::Executor &executor) -> void;

    // Create the fiber by given entry
    static auto create4(FiberEntry entry, runtime::CaptureSource source = {}) -> FiberContext *;

    // Create the fiber by given callable
    template <typename Fn, typename ...Args>
    static auto create(Fn fn, Args ...args) -> FiberContext *;
protected:
    // Get the pointer of invoke return, it may throw the exception the fiber throw
    auto valuePointer() -> void *;

    FiberContext() = default;
    ~FiberContext() = default;
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
        return mHandle->resume();
    }

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> void;
protected:
    auto onStopRequested() -> void;
    static auto onCompletion(FiberContext *ctxt, void *_self) -> void;

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
    auto result = static_cast<Option<T> *>(valuePointer());
    return unwrapOption(std::move(*result));
}

template <typename Fn, typename ...Args>
inline auto FiberContext::create(Fn fn, Args ...args) -> FiberContext * {
    using Callable = FiberCallable<Fn, Args...>;
    return create4({
        .cleanup = Callable::cleanup,
        .invoke = Callable::invoke,
        .args = new Callable {std::forward<Fn>(fn), std::forward<Args>(args)...},
    });
}

} // namespace fiber

// Operations on the current fiber
namespace this_fiber {

// Get the current fiber's stop token
extern auto ILIAS_API stopToken() -> runtime::StopToken;

// Yield the current fiber, resume the fiber when the next time it is scheduled
extern auto ILIAS_API yield() -> void;

// INTERNAL!!!, wait the stackless CoroHandle to done or stopped
extern auto ILIAS_API await4(runtime::CoroHandle handle, runtime::CaptureSource source = {}) -> void;

template <typename T>
inline auto await(Task<T> task, runtime::CaptureSource source = {}) -> T {
    auto handle = task::TaskHandle<T>{task._handle()};
    await4(handle, source);
    return handle.value();
}

template <Awaitable T>
inline auto await(T awaitable, runtime::CaptureSource source = {}) -> AwaitableResult<T> {
    return await(toTask(std::move(awaitable)), source);
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
    auto wait(runtime::CaptureSource where = {}) -> T {
        auto executor = runtime::Executor::currentThread();
        ILIAS_ASSERT(mHandle, "Can't wait for an invalid fiber");
        ILIAS_ASSERT(executor, "Can't wait for a fiber without an executor");

        auto handle = std::exchange(mHandle, nullptr);
        handle->setExecutor(*executor);
        handle->wait(where);
        return handle->value<T>();
    }

    // Swap the fiber with another fiber
    auto swap(Fiber &other) -> void {
        mHandle.swap(other.mHandle);
    }

    // Set the context of the fiber, call on await_transform
    auto setContext(runtime::CoroContext &ctxt) -> void {
        mHandle->setExecutor(ctxt.executor());
    }

    auto operator =(const Fiber &) -> Fiber & = delete;
    auto operator =(Fiber &&) -> Fiber & = default;

    // co_await
    auto operator co_await() && -> fiber::FiberAwaiter<T> {
        ILIAS_ASSERT(mHandle, "Can't co_wait an invalid fiber");
        return {std::move(mHandle)};
    }

    // Check the fiber is valid
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    fiber::FiberHandle mHandle;
};

template <typename Fn, typename ...Args>
Fiber(Fn, Args ...) -> Fiber<std::invoke_result_t<Fn, Args...> >;

/**
 * @brief The special exception for fiber cancellation
 * 
 */
class FiberCancellation {};

ILIAS_NS_END