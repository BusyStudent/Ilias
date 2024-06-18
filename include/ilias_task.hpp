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
 * @brief Cancel status for cancel
 * 
 */
enum class CancelStatus {
    Pending, //< This cancel is still pending
    Done,    //< This cancel is done
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

    /**
     * @brief Construct a new Task object
     * 
     * @param h 
     */
    explicit Task(handle_type h) : mHandle(h) { }

    /**
     * @brief Construct a new Task object (deleted)
     * 
     * @param t 
     */
    Task(const Task &t) = delete;

    /**
     * @brief Construct a new Task object by move
     * 
     * @param t 
     */
    Task(Task &&t) : mHandle(t.mHandle) { t.mHandle = nullptr; }

    /**
     * @brief Construct a new empty Task object
     * 
     */
    Task() { }

    /**
     * @brief Destroy the Task object
     * 
     */
    ~Task() { clear(); }

    /**
     * @brief Release the ownship of the internal coroutine handle
     * 
     * @return handle_type 
     */
    auto leak() -> handle_type {
        auto h = mHandle;
        mHandle = nullptr;
        return h;
    }

    /**
     * @brief Get the coroutine handle
     * 
     * @return handle_type 
     */
    auto handle() const -> handle_type {
        return mHandle;
    }

    /**
     * @brief Get the coroutine promise reference
     * 
     * @return promise_type& 
     */
    auto promise() const -> promise_type & {
        return mHandle.promise();
    }
    
    /**
     * @brief Get the name of the coroutine (for debug)
     * 
     * @return const char* 
     */
    auto name() const -> const char * {
        return mHandle.promise().name();   
    }

    /**
     * @brief Cancel the task
     * 
     * @return CancelStatus
     */
    auto cancel() const -> CancelStatus {
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
            StopToken token;
            promise().setStopOnDone(&token);
            eventLoop()->resumeHandle(mHandle);
            eventLoop()->run(token);
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
            // Cancel current
            auto status = cancel();
            ILIAS_ASSERT(status == CancelStatus::Done);
        }
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
     * @brief Access the promise directly
     * 
     * @return promise_type* 
     */
    auto operator ->() const -> promise_type * {
        return &(mHandle.promise());
    }

    /**
     * @brief Check the task is empty or not
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }

    /**
     * @brief Create task by the callable and args
     * 
     * @tparam Callable 
     * @tparam Args 
     * @return auto 
     */
    template <typename Callable, typename ...Args>
    static auto fromCallable(Callable &&callable, Args &&...args);
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
            auto await_suspend(std::coroutine_handle<>) const noexcept -> void { self->mSuspended = true; }
            auto await_resume() { ILIAS_ASSERT(self); self->mStarted = true; self->mSuspended = false; }
            PromiseBase *self;
        };
        return Awaiter {this};
    }

    auto final_suspend() noexcept -> SwitchCoroutine {
        mSuspended = true; //< Done is still suspended
        if (mStopOnDone) [[unlikely]] {
            mStopOnDone->stop();
        }
        if (mDestroyOnDone) [[unlikely]] {
            mEventLoop->destroyHandle(mHandle);
        }
        // If has someone is waiting us and he is suspended
        // We can not resume a coroutine which is not suspended, It will cause UB
        if (mPrevAwaiting) {
            ILIAS_ASSERT(mPrevAwaiting->isResumable());
            mPrevAwaiting->setResumeCaller(this);
            return mPrevAwaiting->mHandle;
        }
        return std::noop_coroutine();
    }

    /**
     * @brief Unhandled Exception on Coroutine, default in terminate
     * 
     */
    auto unhandled_exception() noexcept -> void {
        std::terminate();
    }

    /**
     * @brief Cancel the current coroutine
     * 
     * @return CancelStatus
     */
    auto cancel() -> CancelStatus {
        mCanceled = true;
        if (!mSuspended) {
            ILIAS_CTRACE("[Task] Cancel on a still running task\n");
            return CancelStatus::Pending;
        }
        while (!mHandle.done()) {
            mHandle.resume();
        }
        return CancelStatus::Done;
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

    auto isSuspended() const -> bool {
        return mSuspended;
    }

    auto isResumable() const -> bool {
        return mSuspended && !mHandle.done();
    }

    auto name() const -> const char * {
        return mName;
    }

    auto handle() const -> std::coroutine_handle<> {
        return mHandle;
    }

    /**
     * @brief Get the pointer of the promise which resume us
     * 
     * @return PromiseBase* 
     */
    auto resumeCaller() const -> PromiseBase * {
        return mResumeCaller;
    }

    /**
     * @brief Set the Stop On Done object, the token's stop() method will be called when the promise done
     * 
     * @param token The token pointer
     */
    auto setStopOnDone(StopToken *token) noexcept -> void {
        mStopOnDone = token;
    }

    /**
     * @brief Set the Suspended object
     * @internal Don't use this, it's for internal use, by AwaitRecorder
     * 
     * @param suspended 
     */
    auto setSuspended(bool suspended) noexcept -> void {
        mSuspended = suspended;
    }

    /**
     * @brief Let it destroy on the coroutine done
     * 
     */
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

    /**
     * @brief Set the Event Loop object
     * 
     * @param eventLoop 
     */
    auto setEventLoop(EventLoop *eventLoop) noexcept -> void {
        mEventLoop = eventLoop;
    }
protected:
    bool mStarted = false;
    bool mCanceled = false;
    bool mSuspended = false; //< Is the coroutine suspended at await ?
    bool mDestroyOnDone = false;
    const char *mName = nullptr;
    StopToken *mStopOnDone = nullptr; //< The token will be stop on this promise done
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
class TaskPromise final : public PromiseBase {
public:
    using handle_type = std::coroutine_handle<TaskPromise<T> >;
    using value_type = Result<T>;
    using return_type = T;

    /**
     * @brief Construct a new Task Promise object
     * 
     */
    TaskPromise(ILIAS_CAPTURE_CALLER(name)) noexcept {
        mName = name.function_name();
    }

    ~TaskPromise() noexcept {
        ILIAS_CO_REMOVE(mHandle); //< Self is destroy, remove this
        ILIAS_ASSERT(!mException); //< If exception was no still in there, abort!
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
        return AwaitRecorder {this, AwaitTransform<U>().transform(this, std::forward<U>(t))};
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
        return AwaitRecorder<U&&> {this, std::forward<U>(t)};
    }

    /**
     * @brief Get the return object object
     * 
     * @return Task<T> 
     */
    auto get_return_object() -> Task<T> {
        auto handle = handle_type::from_promise(*this);
        mHandle = handle;
        ILIAS_CO_ADD(mHandle); //< Add to the alive coroutine set
        return Task<T>(handle);
    }

    /**
     * @brief For task it will catch BadExpectedAccess<Error> and put it to the value
     * 
     */
    auto unhandled_exception() noexcept -> void {
        try {
            throw;
        }
        catch (BadExpectedAccess<Error> &err) {
            mValue.construct(Unexpected(err.error()));
        }
        catch (...) {
            mException = std::current_exception();
        }
    }

    /**
     * @brief Get the stored return value, do not double call it or call it when the coroutine is still not done
     * 
     * @return Result<T> 
     */
    auto value() -> Result<T> {
        if (mException) {
            auto exception = mException;
            mException = nullptr;
            std::rethrow_exception(exception);
        }
        ILIAS_ASSERT_MSG(mHandle.done(), "Task<T> is not done yet!");
        ILIAS_ASSERT_MSG(mValue.hasValue(), "Task<T> doesn't has value, do you forget co_return?");
        return std::move(*mValue);
    }

    /**
     * @brief Return the value, the any args version
     * 
     * @tparam U 
     * @param value 
     */
    template <typename U>
    auto return_value(U &&value) noexcept(std::is_nothrow_constructible_v<Result<T>, U>) -> void {
        mValue.construct(std::forward<U>(value));
    }

    /**
     * @brief Return the value, the moveable result<T> version
     * 
     * @tparam har 
     * @param value 
     */
    template <char = 0>
    auto return_value(Result<T> &&value) noexcept(std::is_nothrow_move_constructible_v<Result<T> >) -> void {
        mValue.construct(std::move(value));
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
 * @brief Helper class for record the suspend flags
 * 
 * @tparam T 
 */
template <typename T>
class AwaitRecorder {
public:
    auto await_ready() -> bool { 
        return mAwaiter.await_ready(); 
    }
    template <typename U>
    auto await_suspend(std::coroutine_handle<TaskPromise<U> > handle) {
        ILIAS_ASSERT(&handle.promise() == mPromise); //< Is same promise? 
        ILIAS_ASSERT(!mPromise->isSuspended());

        mPromise->setSuspended(true);
        return mAwaiter.await_suspend(handle);
    }
    auto await_resume() {
        mPromise->setSuspended(false);
        return mAwaiter.await_resume();
    }

    PromiseBase *mPromise;
    T mAwaiter;
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
    auto cancel() -> CancelStatus;
    auto isDone() const -> bool;
    auto isCanceled() const -> bool;
    auto operator =(CancelHandle &&h) -> CancelHandle &;
    auto operator =(std::nullptr_t) -> CancelHandle &;
    explicit operator bool() const noexcept { return mPtr; }
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
class JoinHandle final : public CancelHandle {
public:
    using handle_type = typename Task<T>::handle_type;

    explicit JoinHandle(handle_type handle) : CancelHandle(handle) { }
    JoinHandle(std::nullptr_t) : CancelHandle(nullptr) { }
    JoinHandle(const JoinHandle &) = delete;
    JoinHandle(JoinHandle &&) = default;
    JoinHandle() = default;

    /**
     * @brief Blocking wait to get the result
     * 
     * @return Result<T> 
     */
    auto join() -> Result<T>;
    auto joinable() const noexcept -> bool;
    auto operator =(JoinHandle &&other) -> JoinHandle & = default;
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
        h.destroy(); //< Done, we destroy 
    }
    mPtr = nullptr;
}
inline auto CancelHandle::_cohandle() const -> std::coroutine_handle<> {
    if (mPtr) {
        return mPtr->handle();
    }
    return std::coroutine_handle<>();
}
inline auto CancelHandle::cancel() -> CancelStatus {
    if (mPtr) {
        return mPtr->cancel();
    }
    return CancelStatus::Done;
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
inline auto CancelHandle::operator =(std::nullptr_t) -> CancelHandle & {
    clear();
    return *this;
}

// JoinHandle
template <typename T>
inline auto JoinHandle<T>::join() -> Result<T> {
    ILIAS_ASSERT(joinable());
    auto h = static_cast<TaskPromise<T> *>(mPtr)->handle();
    // If not done, we try to wait it done by enter the event loop
    if (!h.done()) {
        StopToken token;
        h.promise().setStopOnDone(&token);
        h.promise().eventLoop()->run(token);
    }
    // Get the value and drop the task
    auto value = h.promise().value();
    clear();
    return value;
}
template <typename T>
inline auto JoinHandle<T>::joinable() const noexcept -> bool {
    return mPtr;
}


// Task
template <typename T>
template <typename Callable, typename ...Args>
inline auto Task<T>::fromCallable(Callable &&callable, Args &&...args) {
    using TaskType = std::invoke_result_t<Callable, Args...>;
    static_assert(IsTask<TaskType>, "Invoke result must be a task");
    if constexpr(!std::is_class_v<Callable> || std::is_empty_v<Callable>) {
        return std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...);
    }
    else { //< Make callable alive, for lambda with capturing values
        return [](auto callable, auto ...args) -> Task<typename TaskType::value_type> {
            co_return co_await std::invoke(callable, args...);
        }(std::forward<Callable>(callable), std::forward<Args>(args)...);
    }
}

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
    return postTask(Task<>::fromCallable(std::forward<Callable>(callable), std::forward<Args>(args)...));
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