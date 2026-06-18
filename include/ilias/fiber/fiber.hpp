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

// The entry point of the fiber
class FiberEntry {
public:
    void  (*destroy)(FiberEntry *self) = nullptr; // For cleanup the args, destroy when the fiber context is destroyed
    void* (*invoke)(FiberEntry *self) = nullptr; // The entry point of the fiber
    size_t  stackSize = 0; // The stack size of the fiber (0 for default)
};

// The context of the fiber (user part)
class ILIAS_API FiberContext {
public:
    class Deleter {
    public:
        auto operator()(FiberContext *ctxt) -> void {
            ctxt->destroy();
        }
    };

    // Resume the fiber, return bool for whether the fiber is done
    [[nodiscard]]
    auto resume() -> bool;

    // Blocking wait the fiber to be done
    auto wait(runtime::CaptureSource where) -> void;

    // Destroy the fiber
    auto destroy() -> void;

    // Get the result of the fiber
    template <typename T>
    [[nodiscard]]
    auto value() -> T;

    // Set the executor
    auto setExecutor(runtime::Executor &executor) -> void;

    // Create the fiber by given entry, it take the ownership of the entry
    static auto create4(FiberEntry *entry) -> FiberContext *;
protected:
    // Get the pointer of invoke return, it may throw the exception the fiber throw
    auto valuePointer() -> void *;

    FiberContext() = default;
    ~FiberContext() = default;
};

// RAII handle for fiber
using FiberHandle = std::unique_ptr<FiberContext, FiberContext::Deleter>;

// The callable of the fiber, for store the fn, args and the return value, it should be allocated on heap
template <typename Fn, typename ...Args>
class FiberCallable final : public FiberEntry {
public:
    using T = std::invoke_result_t<Fn, Args...>;

    FiberCallable(Fn fn, Args ...args) : mFn(std::forward<Fn>(fn)), mArgs(std::forward<Args>(args)...) {
        // Setup vtable
        this->invoke = &FiberCallable::onInvoke;
        this->destroy = &FiberCallable::onDestroy;
    }
    FiberCallable(const FiberCallable &) = delete;
private:
    static auto onInvoke(FiberEntry *_self) -> void * {
        auto self = static_cast<FiberCallable *>(_self);
        self->mValue = makeOption([&]() {
            return std::apply(self->mFn, self->mArgs);
        });
        return &self->mValue;
    }

    static auto onDestroy(FiberEntry *_self) -> void {
        delete static_cast<FiberCallable *>(_self);
    }

    // State
    Option<T> mValue; // Put the result here
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    Fn mFn;
    [[ILIAS_NO_UNIQUE_ADDRESS]] // The args may empty
    std::tuple<Args...> mArgs;
};

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
class FiberAwaiter final : public FiberAwaiterBase {
public:
    using FiberAwaiterBase::FiberAwaiterBase;

    auto await_resume() -> T {
        return mHandle->value<T>();
    }
};

template <typename T>
inline auto FiberContext::value() -> T {
    auto result = static_cast<Option<T> *>(valuePointer());
    ILIAS_ASSERT(result, "The result pointer is null, INTERNAL BUG???");
    return unwrapOption(std::move(*result));
}

// Initialize the fiber environment (reference count)
extern auto ILIAS_API initialize() -> void;
extern auto ILIAS_API shutdown() -> void;

} // namespace fiber

// Operations on the current fiber
namespace this_fiber {

// Get the current fiber's stop token
extern auto ILIAS_API stopToken() -> runtime::StopToken;

// Yield the current fiber, resume the fiber when the next time it is scheduled
extern auto ILIAS_API yield() -> void;

// INTERNAL!!!, wait the stackless CoroHandle to done or stopped
extern auto ILIAS_API awaitImpl(runtime::CoroHandle handle, runtime::CaptureSource source) -> void;

// Functor for impl await
class Await {
public:
    template <typename T>
    auto operator ()(Task<T> task, runtime::CaptureSource source = {}) const -> T {
        task::TaskHandle<T> handle {task._handle()};
        awaitImpl(handle, source);
        ILIAS_ASSUME(handle, "This handle still exists");
        return handle.value();
    }

    template <Awaitable T>
    auto operator ()(T awaitable, runtime::CaptureSource source = {}) const -> AwaitableResult<T> {
        return operator ()(toTask(std::move(awaitable)), source);
    }
};

// For chain, doSomething() | await
template <Awaitable T>
inline auto operator |(T awaitable, Await tags) -> AwaitableResult<T> {
    return tags(std::move(awaitable));
}

/**
 * @brief Await given awaitable in current fiber
 * 
 * @param awaitable
 * @return AwaitableResult<T>
 */
constexpr inline Await await {};

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
    Fiber(Fiber &&) = default;

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
    explicit Fiber(Fn fn, Args ...args) {
        auto callable = new fiber::FiberCallable<Fn, Args...> {std::forward<Fn>(fn), std::forward<Args>(args)...};
        mHandle.reset(fiber::FiberContext::create4(callable));
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

    // Operator
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

/**
 * @brief An RAII guard to initialize and shutdown the fiber environment (refcounted), it is recommended to use it before create any fiber
 * @note It convert thread to Fiber on Win32, it current thread already fiber, no-op, and it's a no-op on other platforms
 * 
 * @code
 *  FiberInitializer initializer;
 *  Fiber fiber([]() { return 42; });
 * @endcode 
 * 
 */
class FiberInitializer {
public:
    FiberInitializer() {
        fiber::initialize();
    }
    ~FiberInitializer() {
        fiber::shutdown();
    }
    FiberInitializer(const FiberInitializer &) = delete;
};

template <typename Fn, typename ...Args>
Fiber(Fn, Args ...) -> Fiber<std::invoke_result_t<Fn, Args...> >;

/**
 * @brief The special exception for fiber cancellation
 * 
 */
class FiberCancellation final {};

ILIAS_NS_END