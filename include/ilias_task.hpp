#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <functional> //< for std::invoke
#include "ilias.hpp"
#include "ilias_co.hpp"
#include "ilias_expected.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Check a type is Task<T>
 * 
 * @tparam T 
 */
template <typename T>
concept IsTask = requires(T t) {
    t.handle();
    t.promise();
    t.cancel();  
};

/**
 * @brief A Lazy way to get value
 * 
 * @tparam T 
 */
template <typename T>
class Task {
public:
    using promise_type = TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using result_type = Result<T>;
    using value_type = T;

    explicit Task(handle_type h) : mHandle(h) { }
    Task(const Task &t) = delete;
    Task(Task &&t) : mHandle(t.mHandle) { t.mHandle = nullptr; }
    Task() { }
    ~Task() { clear(); }

    auto leak() -> handle_type {
        auto h = mHandle;
        mHandle = nullptr;
        return h;
    }
    auto handle() const -> handle_type {
        return mHandle;
    }
    auto promise() const -> promise_type & {
        return mHandle.promise();
    }
    auto name() const -> const char * {
        return mHandle.promise().name();   
    }
    /**
     * @brief Cancel the task
     * 
     */
    auto cancel() const -> void {
        ILIAS_CTRACE("[Ilias] Task<{}> Canceling {}", typeid(T).name(), name());
        return promise().cancel();
    }
    /**
     * @brief Get the task's event loop
     * 
     * @return EventLoop* 
     */
    auto eventLoop() const -> EventLoop * {
        return promise().eventLoop();
    }
    /**
     * @brief Get the result value
     * 
     * @return result_type 
     */
    auto value() const -> result_type {
        if (!mHandle.done()) {
            promise().setQuitOnDone();
            eventLoop()->resumeHandle(mHandle);
            eventLoop()->run();
        }
        return promise().value();
    }
    /**
     * @brief Clear the task
     * 
     */
    auto clear() -> void {
        if (!mHandle) {
            return;
        }
        if (!mHandle.done()) {
            // Cancel
            cancel();
        }
        ILIAS_CO_REMOVE(mHandle);
        mHandle.destroy();
        mHandle = nullptr;
    }
    /**
     * @brief Assign a moved task
     * 
     * @param other 
     * @return Task& 
     */
    auto operator =(Task &&other) -> Task & {
        clear();
        mHandle = other.mHandle;
        other.mHandle = nullptr;
        return *this;
    }
    auto operator =(const Task &) -> Task & = delete;
    /**
     * @brief Check the task is empty or not
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    handle_type mHandle;
};

/**
 * @brief All Promise's base
 * 
 */
class PromiseBase {
public:
    PromiseBase() = default;
    PromiseBase(const PromiseBase &) = delete; 
    ~PromiseBase() = default;

    auto initial_suspend() noexcept {
        struct Awaiter {
            auto await_ready() const noexcept -> bool { return false; }
            auto await_suspend(std::coroutine_handle<>) const noexcept -> void { }
            auto await_resume() { ILIAS_ASSERT(self); self->mStarted = true; }
            PromiseBase *self;
        };
        return Awaiter {this};
    }
    auto final_suspend() noexcept -> SwitchCoroutine {
        if (mQuitOnDone) [[unlikely]] {
            mEventLoop->quit();
        }
        if (mDestroyOnDone) [[unlikely]] {
            mEventLoop->destroyHandle(mHandle);
        }
        if (mPrevAwaiting) {
            mPrevAwaiting->setResumeCaller(this);
            return mPrevAwaiting->mHandle;
        }
        return std::noop_coroutine();
    }
    auto unhandled_exception() noexcept -> void {
        mException = std::current_exception();
    }
    /**
     * @brief Cancel the current coroutine
     * 
     */
    auto cancel() noexcept -> void {
        mCanceled = true;
        while (!mHandle.done()) {
            mHandle.resume();
        }
    }
    auto eventLoop() const -> EventLoop * {
        return mEventLoop;
    }
    auto isCanceled() const -> bool {
        return mCanceled;
    }
    auto isStarted() const -> bool {
        return mStarted;
    }
    auto name() const -> const char * {
        return mName;
    }
    auto handle() const -> std::coroutine_handle<> {
        return mHandle;
    }
    auto resumeCaller() const -> PromiseBase * {
        return mResumeCaller;
    }
    auto setQuitOnDone() noexcept -> void {
        mQuitOnDone = true;
    }
    auto setDestroyOnDone() noexcept -> void {
        mDestroyOnDone = true;
    }
    /**
     * @brief Set the Resume Caller object
     * 
     * @param caller Who resume us
     */
    auto setResumeCaller(PromiseBase *caller) noexcept -> void {
        mResumeCaller = caller;
    }
    /**
     * @brief Set the Prev Awaiting object, the awaiting will be resumed when the task done
     * 
     * @param awaiting 
     */
    auto setPrevAwaiting(PromiseBase *awaiting) noexcept -> void {
        mPrevAwaiting = awaiting;
    }
    auto setEventLoop(EventLoop *eventLoop) noexcept -> void {
        mEventLoop = eventLoop;
    }
protected:
    bool mStarted = false;
    bool mCanceled = false;
    bool mQuitOnDone = false;
    bool mDestroyOnDone = false;
    const char *mName = nullptr;
    EventLoop *mEventLoop = EventLoop::instance();
    PromiseBase *mPrevAwaiting = nullptr;
    PromiseBase *mResumeCaller = nullptr;
    std::exception_ptr mException = nullptr;
    std::coroutine_handle<> mHandle = nullptr;
};

/**
 * @brief A typed task promise
 * 
 * @tparam T 
 */
template <typename T>
class TaskPromise : public PromiseBase {
public:
    using handle_type = std::coroutine_handle<TaskPromise<T> >;
    using value_type = Result<T>;
    using return_type = T;

    /**
     * @brief Construct a new Task Promise object
     * 
     */
    TaskPromise(ILIAS_CAPTURE_CALLER(name)) {
        mName = name.function_name();
        ILIAS_CTRACE("[Ilias] Task<{}> created {}", typeid(T).name(), mName);
    }
    ~TaskPromise() {
        ILIAS_CTRACE("[Ilias] Task<{}> destroyed {}", typeid(T).name(), mName);
    }
    /**
     * @brief Transform user defined type
     * 
     * @tparam T must be NotAwaiter
     * @param t 
     * @return auto 
     */
    template <NotAwaiter U>
    auto await_transform(U &&t) noexcept {
        return AwaitTransform<U>().transform(this, std::forward<U>(t));
    }
    /**
     * @brief Passthrough Awaiter
     * 
     * @tparam T must be Awaiter
     * @param t 
     * @return T 
     */
    template <Awaiter U>
    auto await_transform(U &&t) noexcept {
        return std::forward<U>(t);
    }

    auto get_return_object() -> Task<T> {
        auto handle = handle_type::from_promise(*this);
        mHandle = handle;
        ILIAS_CO_ADD(mHandle); //< Add to the alive coroutine set
        return Task<T>(handle);
    }
    /**
     * @brief Get the stored return value
     * 
     * @return Result<T> 
     */
    auto value() -> Result<T> {
        if (mException) {
            std::rethrow_exception(mException);
        }
        ILIAS_ASSERT(mValue.hasValue());
        return std::move(*mValue);
    }
    template <typename U>
    auto return_value(U &&value) -> void {
        mValue.construct(std::forward<U>(value));
    }
    /**
     * @brief Get the promise handle type
     * 
     * @return handle_type 
     */
    auto handle() const -> handle_type {
        return handle_type::from_address(mHandle.address());
    }
private:
    Uninitialized<Result<T> > mValue;
};

/**
 * @brief Handle used to observe the running task
 * 
 */
class CancelHandle {
public:
    template <typename T>
    explicit CancelHandle(std::coroutine_handle<T> handle) : mPtr(&handle.promise()) { }
    CancelHandle(const CancelHandle &) = delete;
    CancelHandle(CancelHandle &&other) : mPtr(other.mPtr) { other.mPtr = nullptr; }
    CancelHandle(std::nullptr_t) { }
    CancelHandle() = default;
    ~CancelHandle() { clear(); }

    auto clear() -> void;
    auto cancel() -> void;
    auto isDone() const -> bool;
    auto isCanceled() const -> bool;
    auto operator =(CancelHandle &&h) -> CancelHandle &;
private:
    auto _cohandle() const -> std::coroutine_handle<>;
protected:
    PromiseBase *mPtr = nullptr;
};

/**
 * @brief Handle used to observe the running task but, it can blocking wait for it
 * 
 * @tparam T 
 */
template <typename T>
class JoinHandle : public CancelHandle {
public:
    explicit JoinHandle(std::coroutine_handle<TaskPromise<T> > handle) : CancelHandle(handle) { }
    JoinHandle(const JoinHandle &) = delete;
    JoinHandle(JoinHandle &&) = default;

    auto join() const -> Result<T>;
    auto operator =(JoinHandle &&other) -> JoinHandle & = default;
private:
    auto _cohandle() const -> std::coroutine_handle<TaskPromise<T> > {
        return static_cast<TaskPromise<T> *>(mPtr)->handle();
    }
};

// CancelHandle
inline auto CancelHandle::clear() -> void {
    auto h = _cohandle();
    if (!h) {
        return;
    }
    if (!h.done()) {
        // Still not done, we detach it
        mPtr->setDestroyOnDone();
    }
    else {
        h.destroy(); //< Done, we destroy it
    }
    mPtr = nullptr;
}
inline auto CancelHandle::_cohandle() const -> std::coroutine_handle<> {
    if (mPtr) {
        return mPtr->handle();
    }
    return std::coroutine_handle<>();
}
inline auto CancelHandle::cancel() -> void {
    if (mPtr) {
        mPtr->cancel();
    }
}
inline auto CancelHandle::isDone() const -> bool {
    return _cohandle().done();
}
inline auto CancelHandle::isCanceled() const -> bool {
    return mPtr->isCanceled();
}
inline auto CancelHandle::operator =(CancelHandle &&other) -> CancelHandle & {
    if (this == &other) {
        return *this;
    }
    clear();
    mPtr = other.mPtr;
    other.mPtr = nullptr;
    return *this;
}
// template <typename T>
// inline auto JoinHandle<T>::join() const -> Result<T> {
//     ILIAS_ASSERT(mPtr);

// }

// Some EventLoop 
template <typename T>
inline auto EventLoop::runTask(const Task<T> &task) {
    task.promise().setEventLoop(this);
    return task.value();
}
template <typename T>
inline auto EventLoop::postTask(Task<T> &&task) {
    auto handle = task.leak();
    handle.promise().setEventLoop(this);
    resumeHandle(handle);
    return JoinHandle<T>(handle);
}
template <typename Callable, typename ...Args>
inline auto EventLoop::spawn(Callable &&callable, Args &&...args) {
    using TaskType = std::invoke_result_t<Callable, Args...>;
    static_assert(IsTask<TaskType>, "Invoke result must be a task");
    if constexpr(!std::is_class_v<Callable> || std::is_empty_v<Callable>) {
        return postTask(std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...));
    }
    else { //< Make callable alive, for lambda with capturing values
        return postTask([](auto callable, auto ...args) -> Task<typename TaskType::value_type> {
            co_return co_await std::invoke(callable, args...);
        }(std::forward<Callable>(callable), std::forward<Args>(args)...));
    }
}

// Helper operators
template <typename T>
inline auto operator <<(EventLoop *eventLoop, Task<T> &&task) {
    return eventLoop->postTask(std::move(task));
}
template <std::invocable Callable>
inline auto operator <<(EventLoop *eventLoop, Callable &&callable) {
    return eventLoop->spawn(std::forward<Callable>(callable));
}
template <typename T>
inline auto operator >>(EventLoop *eventLoop, const Task<T> &task) {
    return eventLoop->runTask(task);
}
template <typename Callable, typename ...Args>
inline auto co_spawn(Callable &&callable, Args &&...args) {
    return EventLoop::instance()->spawn(std::forward<Callable>(callable), std::forward<Args>(args)...);
}

ILIAS_NS_END