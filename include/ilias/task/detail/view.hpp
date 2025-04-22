/**
 * @file view.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the TaskView<T> class, it is a view of the task
 * @version 0.1
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/detail/promise.hpp>
#include <ilias/cancellation_token.hpp>

ILIAS_NS_BEGIN

namespace detail {
    struct TaskViewNull { };
}

/**
 * @brief The cancellation policy for the coroutine
 * 
 */
enum class CancelPolicy {
    Once,         // Cancels only the current await point; subsequent co_await operations proceed normally (default)
    Persistent,   // Cancellation persists; all subsequent co_await operations will receive cancellation notifications
};

/**
 * @brief The type erased coroutine, it can observe the coroutine, 
 *  it is a superset of std::coroutine_handle<> and has interface to access the coroutine's promise field
 * 
 */
class CoroHandle {
public:
    CoroHandle() = default;
    CoroHandle(std::nullptr_t) { }

    /**
     * @brief Construct a CoroHandle from a Task's or Generator coroutine handle
     * 
     * @tparam T 
     * @param handle 
     */
    template <detail::IsCoroPromise T>
    CoroHandle(std::coroutine_handle<T> handle) : mPromise(&handle.promise()), mHandle(handle) { }

    /**
     * @brief Check if the coroutine is done
     * 
     * @return bool 
     */
    auto done() const noexcept { return mHandle.done(); }

    /**
     * @brief Resume the coroutine
     * 
     */
    auto resume() const noexcept { 
        return mHandle.resume();
    }

    /**
     * @brief Schedule the coroutine in the executor (thread safe)
     * @warning Don't call this function if the coroutine is already scheduled
     * 
     * @return auto 
     */
    auto schedule() const noexcept { 
        return executor().post(scheduleImpl, mHandle.address());
    }

    /**
     * @brief Destroy the coroutine
     * 
     * @return auto 
     */
    auto destroy() const noexcept { 
        ILIAS_ASSERT(isSafeToDestroy()); 
        return mHandle.destroy(); 
    }

    /**
     * @brief Destroy the coroutine later in the executor (thread safe)
     * @warning Don't call this function if the coroutine is already scheduled to destroy
     * 
     * @return auto 
     */
    auto destroyLater() const noexcept {
        return executor().post(destroyLaterImpl, mHandle.address());
    }

    /**
     * @brief Send a cancel request to the coroutine
     * 
     */
    auto cancel() const { return cancellationToken().cancel(); }

    /**
     * @brief Get the cancellation token of the coroutine
     * 
     * @return CancellationToken& 
     */
    auto cancellationToken() const noexcept -> CancellationToken & { return mPromise->cancellationToken(); }

    /**
     * @brief Check if the coroutine is cancellation requested
     * 
     * @return bool
     */
    auto isCancellationRequested() const noexcept -> bool { return cancellationToken().isCancellationRequested(); }

    /**
     * @brief Check if the coroutine is started
     * 
     * @return true 
     * @return false 
     */
    auto isStarted() const noexcept -> bool { return mPromise->isStarted(); }

    /**
     * @brief Check the coroutine is safe to destroy
     * 
     * @return bool 
     */
    auto isSafeToDestroy() const noexcept -> bool { return !isStarted() || done(); }

    /**
     * @brief Get the executor of the coroutine
     * 
     * @return Executor &
     */
    auto executor() const noexcept -> Executor & { return mPromise->executor(); }

#if defined(ILIAS_TASK_TRACE)
    /**
     * @brief Get the stack frame of the coroutine (used for tracing)
     * 
     * @internal 
     * @return detail::StackFrame& 
     */
    auto frame() const noexcept -> detail::StackFrame & { return mPromise->frame(); }

    /**
     * @brief Link the stack frame of the coroutine (used for tracing)
     * 
     * @param child The child coroutine
     */
    auto traceLink(CoroHandle child) const noexcept -> void {
        frame().children.push_back(&child.frame());
        child.frame().parent = &frame();
    }
#endif // #if defined(ILIAS_TASK_TRACE)

    /**
     * @brief Set the Awaiting Coroutine object, the coroutine will resume it when self is done
     * 
     * @param handle 
     */
    auto setAwaitingCoroutine(std::coroutine_handle<> handle) const noexcept -> void { 
        return mPromise->setAwaitingCoroutine(handle); 
    }

    /**
     * @brief Set the Cancel Policy object, the coroutine will follow this policy when self is cancelled
     * 
     * @param policy 
     */
    auto setCancelPolicy(CancelPolicy policy) const noexcept -> void { 
        return cancellationToken().setAutoReset(policy == CancelPolicy::Once); 
    }

    /**
     * @brief Set the Executor object, the coroutine will use this executor to schedule itself
     * 
     * @param executor 
     */
    auto setExecutor(Executor &executor) const noexcept -> void { 
        return mPromise->setExecutor(executor); 
    }

    /**
     * @brief Set the Cancellation Token object, the coroutine will use this token to receive the cancellation request
     * 
     * @param token 
     */
    auto setCancellationToken(CancellationToken &token) const noexcept -> void {
        return mPromise->setCancellationToken(token);
    }

    /**
     * @brief Register a callback function to be called when the coroutine is done
     * 
     * @param fn 
     * @param data 
     * @return auto 
     */
    auto registerCallback(void (*fn)(void *), void *data) const noexcept -> void { 
        return mPromise->registerCallback(fn, data); 
    }

    /**
     * @brief Register a callback function to be called when the coroutine is done
     * 
     * @param fn 
     */
    auto registerCallback(detail::MoveOnlyFunction<void()> fn) const noexcept -> void { 
        return mPromise->registerCallback(std::move(fn)); 
    }

    /**
     * @brief Allow comparison with other CoroHandle objects
     * 
     */
    auto operator <=>(const CoroHandle &other) const noexcept = default;

    /**
     * @brief Resume the coroutine
     * 
     */
    auto operator ()() const noexcept {
        return mHandle();
    }

    /**
     * @brief Cast to std::coroutine_handle<>
     * 
     * @return std::coroutine_handle<> 
     */
    operator std::coroutine_handle<>() const noexcept {
        return mHandle;
    }

    /**
     * @brief Check if the view is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept { return bool(mHandle); }
protected:
    /**
     * @brief The callback function to schedule a coroutine
     * 
     * @param ptr 
     */
    static auto scheduleImpl(void *ptr) -> void {
        std::coroutine_handle<>::from_address(ptr).resume();
    }

    /**
     * @brief The callback function to destroy a coroutine later
     * 
     * @param ptr 
     */
    static auto destroyLaterImpl(void *ptr) -> void {
        std::coroutine_handle<>::from_address(ptr).destroy();
    }

    detail::CoroPromiseBase *mPromise = nullptr;
    std::coroutine_handle<>  mHandle = nullptr;
};

template <typename T = detail::TaskViewNull>
class TaskView;

/**
 * @brief The type erased TaskView class, it can observe the task
 * 
 */
template <>
class TaskView<detail::TaskViewNull> : public CoroHandle {
public:
    TaskView() = default;
    TaskView(std::nullptr_t) { }

    /**
     * @brief Construct a new Task View object from any Task's handle
     * 
     * @tparam T 
     * @param handle 
     */
    template <typename T>
    TaskView(std::coroutine_handle<detail::TaskPromise<T> > handle) : CoroHandle(handle) { }
};

/**
 * @brief The TaskView<T> class, it can observe the task
 * 
 * @tparam T 
 */
template <typename T>
class TaskView final : public TaskView<> {
public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    TaskView() = default;
    TaskView(std::nullptr_t) { }

    /**
     * @brief Construct a new Task View object from Task<T>'s handle
     * 
     * @param handle 
     */
    TaskView(handle_type handle) : TaskView<>(handle) { }

    /**
     * @brief Get the handle of the Task
     * 
     * @return handle_type 
     */
    auto handle() const -> handle_type {
        return handle_type::from_address(mHandle.address());
    }

    /**
     * @brief Get the return value of the task
     * 
     * @return T
     */
    auto value() const -> T {
        return static_cast<promise_type *>(mPromise)->value();
    }

    /**
     * @brief Cast to handle_type
     * 
     * @return handle_type 
     */
    operator handle_type() const {
        return handle();
    }

    /**
     * @brief Cast from CoroHandle (note it is dangerous)
     * @warning If types mismatch, it is UB
     * 
     * @param view 
     * @return TaskView<T> 
     */
    static auto cast(CoroHandle coroHandle) {
        auto handle = std::coroutine_handle<>(coroHandle);
        return TaskView<T>(handle_type::from_address(handle.address()));
    }
};

template <typename T = void>
class GeneratorView;

/**
 * @brief The type erased GeneratorView class, it can observe the Generator
 * 
 * @tparam  
 */
template <>
class GeneratorView<void> : public CoroHandle {
public:
    GeneratorView() = default;
    GeneratorView(std::nullptr_t) { }

    /**
     * @brief Construct a new Generator View object from any generator handle
     * 
     * @tparam T 
     * @param handle 
     */
    template <typename T>
    GeneratorView(std::coroutine_handle<detail::GeneratorPromise<T> > handle) : CoroHandle(handle) { }

    /**
     * @brief Rethrow the exception in it if needed
     * 
     * @return auto 
     */
    auto rethrowIfException() const {
        return mPromise->rethrowIfException();
    }
};

template <typename T>
class GeneratorView final : public GeneratorView<> {
public:
    using promise_type = detail::GeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    GeneratorView() = default;
    GeneratorView(std::nullptr_t) { }

    /**
     * @brief Construct a new Generator View object from Generator<T>'s handle
     * 
     * @param handle 
     */
    GeneratorView(handle_type handle) : GeneratorView<>(handle) { }

    /**
     * @brief Get the handle of the Generator
     * 
     * @return handle_type 
     */
    auto handle() const -> handle_type {
        return handle_type::from_address(mHandle.address());
    }

    /**
     * @brief Get the stored value in the generator
     * 
     * @return std::optional<T>& 
     */
    auto value() const -> std::optional<T> & {
        return static_cast<promise_type*>(mPromise)->value();
    }

    /**
     * @brief Cast from CoroHandle (note it is dangerous)
     * @warning If types mismatch, it is UB
     * 
     * @param coroHandle 
     * @return GeneratorView<T>
     */
    static auto cast(CoroHandle coroHandle) {
        auto handle = std::coroutine_handle<>(coroHandle);
        return GeneratorView<T>(handle_type::from_address(handle.address()));
    }
};

ILIAS_NS_END

#if !defined(ILIAS_NO_FORMAT)
ILIAS_FORMATTER(CoroHandle) {
    auto format(const auto &view, auto &ctxt) const {
        return format_to(ctxt.out(), "CoroHandle({})", std::coroutine_handle<>(view).address());
    }
};

ILIAS_FORMATTER(TaskView<>) {
    auto format(const auto &view, auto &ctxt) const {
        return format_to(ctxt.out(), "TaskView({})", std::coroutine_handle<>(view).address());
    }
};

ILIAS_FORMATTER_T(typename T, TaskView<T>) {
    auto format(const auto &view, auto &ctxt) const {
        return format_to(ctxt.out(), "TaskView<{}>({})", typeid(T).name(), std::coroutine_handle<>(view).address());
    }
};

#endif