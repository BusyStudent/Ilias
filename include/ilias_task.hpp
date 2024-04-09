#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
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
    ~Task() {
        if (!mHandle) {
            return;
        }
        if (!mHandle.done()) {
            // Cancel
            cancel();
        }
        mHandle.destroy();
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
    auto cancel() const -> void {
        ILIAS_CTRACE("[Ilias] Task<{}> Canceling {}", typeid(T).name(), name());
        return promise().cancel();
    }
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
            promise().setQuitEventLoop();
            eventLoop()->resumeHandle(mHandle);
            eventLoop()->run();
        }
        return promise().value();
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

    auto initial_suspend() noexcept -> std::suspend_always {        
        return {};
    }
    auto final_suspend() noexcept -> SwitchCoroutine {
        if (mQuitEventLoop) [[unlikely]] {
            mEventLoop->quit();
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
    auto name() const -> const char * {
        return mName;
    }
    auto resumeCaller() const -> PromiseBase * {
        return mResumeCaller;
    }
    auto setQuitEventLoop() noexcept -> void {
        mQuitEventLoop = true;
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
    bool mCanceled = false;
    bool mQuitEventLoop = false;
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
        if (mHasValue) {
            mValue.~Result<T>();
        }
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
        ILIAS_ASSERT(mHasValue);
        mHasValue = false;
        return Result<T>(mValue);
    }
    template <typename U>
    auto return_value(U &&value) -> void {
        mHasValue = true;
        new (&mValue) Result<T>(std::forward<U>(value));
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
    union {
        Result<T> mValue;
        int pad = 0;
    };
    bool mHasValue = false;
};

inline auto EventLoop::resumeHandle(std::coroutine_handle<> handle) noexcept -> void {
    // handle.promise()
    post([](void *addr) {
        auto h = std::coroutine_handle<>::from_address(addr);
        h.resume();
    }, handle.address());
}
template <typename T>
inline auto EventLoop::runTask(const Task<T> &task) {
    task.promise().setEventLoop(this);
    return task.value();
}

ILIAS_NS_END