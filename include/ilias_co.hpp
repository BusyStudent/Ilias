#pragma once

#include <cstdio>
#include <version>
#include <tuple>
#include <variant>
#include <concepts>
#include <coroutine>
#include <exception>
#include <functional>
#include "ilias.hpp"
#include "ilias_expected.hpp"
#include "ilias_source_location.hpp"

#if !defined(__cpp_lib_coroutine)
#error "Compiler does not support coroutines"
#endif

#if  defined(ILIAS_NO_SOURCE_LOCATION)
#undef ILIAS_COROUTINE_TRACE
#endif

ILIAS_NS_BEGIN

// --- Coroutine 
template <typename T>
class Task;
template <typename T>
class Promise;
class PromiseRef;
class PromiseBase;
template <typename T>
class AwaitTransform;
template <typename T>
class AwaitWrapper;
class ResumeHandle;
class AbortHandle;
template <typename T>
class JoinHandle;

/**
 * @brief Check this type user has defined transform
 * 
 * @tparam T 
 */
template <typename T>
concept AwaitTransformable = requires(AwaitTransform<T> t) {
    t.transform(T());
};
/**
 * @brief Check this type can be directly pass to co_await
 * 
 * @tparam T 
 */
template <typename T>
concept Awaitable = requires(T t) {
    t.await_ready();
    t.await_resume();
};

template <typename T>
concept Resumeable = requires(T t) {
    t.resume();
};

/**
 * @brief Get Awaitable's result
 * 
 * @tparam T 
 */
template <Awaitable T> 
using AwaitableResult = decltype(std::declval<T>().await_resume());
/**
 * @brief Get AwaitTransformable's result
 * 
 * @tparam T 
 */
template <AwaitTransformable T>
using AwaitTransformResult = decltype(std::declval<AwaitTransform<T> >().transform(std::declval<T>()));

/**
 * @brief Abstraction of event loop
 * 
 */
class EventLoop {
public:
    enum TimerFlags : int {
        TimerDefault    = 0 << 0,
        TimerSingleShot = 1 << 0, //< This timer is single shot (it will auto remove self)
    };

    virtual void requestStop() = 0;
    virtual void run() = 0;
    virtual void post(void (*fn)(void *), void *arg = nullptr) = 0;
    virtual bool delTimer(uintptr_t timer) = 0;
    virtual uintptr_t addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags = 0) = 0;

    /**
     * @brief Spawn a task add post it
     * 
     * @tparam Callable 
     * @tparam Args 
     * @param callable 
     * @param args 
     */
    template <typename Callable, typename ...Args>
    AbortHandle spawn(Callable &&callable, Args &&...args);
    /**
     * @brief Post a task
     * 
     * @tparam T 
     * @param task 
     */
    template <typename T>
    AbortHandle postTask(Task<T> &&task);
    /**
     * @brief Resume the giving handle at event loop, it will be destroyed if done, (erase type by PromiseBase)
     * 
     * @tparam T 
     * @param co 
     */
    void postCoroutine(PromiseBase &promise);
    /**
     * @brief Blocking run task and wait the result
     * 
     * @tparam T 
     * @param task 
     * @return Result<T> 
     */
    template <typename T>
    Result<T> runTask(const Task<T> &task);

    static void quit();
    static EventLoop *instance() noexcept;
    static EventLoop *setInstance(EventLoop *loop) noexcept;
private:
    struct Tls {
        EventLoop *loop = nullptr;
    };
    static Tls &_tls() noexcept;
    static void _invokeCoroutine(void *promiseBase);
protected:
    EventLoop();
    EventLoop(const EventLoop &) = delete;
    ~EventLoop();
};

/**
 * @brief A Lazy way to get value
 * 
 * @tparam T 
 */
template <typename T>
class Task {
public:
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<Promise<T> >;
    using result_type = Result<T>;
    using value_type = T;

    Task() = default;
    Task(const Task &t) = delete;
    Task(Task &&t) : mHandle(t.mHandle) { t.mHandle = nullptr; }
    ~Task() {
        if (mHandle) {
            mHandle.promise().deref();
        }
    }

    handle_type handle() const noexcept {
        return mHandle;
    }
    promise_type &promise() const noexcept {
        return mHandle.promise();
    }
    const char *name() const noexcept {
        return mHandle.promise().name();   
    }
    bool done() const noexcept {
        return mHandle.done();
    }

    Result<T> get() const {
        return promise().value();
    }

    /**
     * @brief Get Task wrapper by coroutine handle
     * 
     * @param handle The handle of the coroutine handle (can not be empty)
     * @param ref should we add the refcount of it ? (true on default)
     * @return Task<T> 
     */
    static Task<T> from(handle_type handle, bool ref = true) {
        Task<T> task;
        task.mHandle = handle;
        if (ref) {
            task.mHandle.promise().ref();
        }
        return task;
    }
private:
    handle_type mHandle;
};

/**
 * @brief The Base of the Promise
 * 
 */
class PromiseBase {
public:
    PromiseBase() {

    }
    PromiseBase(const PromiseBase &) = delete;
    ~PromiseBase() {
        if (mAwaitPromise) {
            mAwaitPromise->deref();
        }
    }
    std::suspend_always initial_suspend() noexcept {
        return {};
    }
    std::suspend_always final_suspend() noexcept {
        if (mAwaitPromise) { //< Resume a coroutinue wating for it
            mAwaitPromise->resume_later();
            mAwaitPromise->deref();
            mAwaitPromise = nullptr;
        }
        return {};
    }
    /**
     * @brief Post a task into event loop
     * 
     * @tparam T 
     * @param task 
     * @return std::suspend_never 
     */
    template <typename T>
    std::suspend_never yield_value(Task<T> &&task) const {
        mEventLoop->postTask(std::move(task));
        return {};
    }
    /**
     * @brief Transform user defined type
     * 
     * @tparam T must be AwaitTransformable
     * @param t 
     * @return auto 
     */
    template <AwaitTransformable T>
    auto await_transform(T &&t) const noexcept {
        return AwaitWrapper(this, AwaitTransform<T>().transform(std::forward<T>(t)));
    }
    /**
     * @brief Passthrough awaitable
     * 
     * @tparam T must be Awaitable
     * @param t 
     * @return T 
     */
    template <Awaitable T>
    auto await_transform(T &&t) const noexcept {
        return AwaitWrapper(this, std::forward<T>(t));
    }
    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }
    void rethrow_if_needed() {
        if (mException) {
            std::rethrow_exception(std::move(mException));
        }
    }
    /**
     * @brief Resume coroutine handle
     * 
     * @return true Done
     * @return false Await
     */
    bool resume_util_done_or_await() {
        while (true) {
            if (mHandle.done()) {
                if (mQuitAtDone) {
                    mEventLoop->requestStop();
                }
                return true;
            }
            if (mSuspendByAwait) {
                return false;
            }
            mHandle.resume();
        }
    }
    /**
     * @brief Abort current coroutine
     * 
     */
    void abort() {
        if (mAborted) {
            return;
        }
#if defined(ILIAS_COROUTINE_TRACE)
        ::fprintf(stderr, "[Ilias] co '%s' was aborted\n", mName);
#endif
        mAborted = true;
        set_suspend_by_await(false);
        resume_util_done_or_await();
        ILIAS_ASSERT(mHandle.done());
    }
    bool aborted() const noexcept {
        return mAborted;
    }
    /**
     * @brief Resume coroutine handle by push it to event loop
     * 
     */
    void resume_later() {
        if (!mHandle.done()) {
            mEventLoop->postCoroutine(*this);
        }
    }
    void ref() noexcept {
        ++mRefcount;
    }
    void deref() noexcept {
        if (--mRefcount == 0) {
            mHandle.destroy();
        }
        ILIAS_ASSERT(mRefcount >= 0);
    }
    bool suspend_by_await() const noexcept {
        return mSuspendByAwait;
    }
    void set_suspend_by_await(bool suspend = true) noexcept {
        mSuspendByAwait = suspend;
    }
    void set_quit_at_done(bool v) noexcept {
        mQuitAtDone = v;
    }
    void set_await_promise(PromiseBase &promise) {
        mAwaitPromise = &promise;
        mAwaitPromise->ref();
    }
    void set_event_loop(EventLoop *event_loop) noexcept {
        mEventLoop = event_loop;
    }
    bool has_exception() const noexcept {
        return bool(mException);
    }
    const char *name() const noexcept {
        return mName;
    }
    EventLoop *event_loop() const noexcept {
        return mEventLoop;
    }
    std::coroutine_handle<> handle() const noexcept {
        return mHandle;
    }
protected:
    bool mSuspendByAwait = false;
    bool mQuitAtDone = false; //< Quit the event loop if coroutinue is doned
    bool mAborted = false; //< Does the task was aborted
    int  mRefcount = 1; //< Inited refcount
    const char *mName = nullptr; //< Name for debug track
    EventLoop *mEventLoop = EventLoop::instance();
    std::exception_ptr mException;
    std::coroutine_handle<> mHandle; //< A Handle pointer to self handle
    PromiseBase            *mAwaitPromise = nullptr; //< Another coroutine await you
};

/**
 * @brief A detail Promise with T
 * 
 * @tparam T 
 */
template <typename T>
class Promise final : public PromiseBase {
public:
    using handle_type = std::coroutine_handle<Promise<T> >;
    using result_type = Result<T>;

#if defined(ILIAS_COROUTINE_TRACE)
    Promise(std::source_location loc = std::source_location::current()) {
        mName = loc.function_name();
        ::fprintf(stderr, "[Ilias] co '%s' was create\n", mName);
    }
    ~Promise() {
        ::fprintf(stderr, "[Ilias] co '%s' was destroy\n", mName);
        if (mHasValue) {
            mStorage.value.~result_type();
        }
    }
#else
    Promise() = default;
    ~Promise() {
        if (mHasValue) {
            mStorage.value.~result_type();
        }
    }
#endif

    template <typename U>
    void return_value(U &&value) noexcept {
        mHasValue = true;

        new (&mStorage.value) result_type(std::forward<U>(value));
    }
    bool has_value() const noexcept {
        return mHasValue;
    }
    /**
     * @brief Get current value
     * 
     * @return Result<T> 
     */
    Result<T> value() {
        ILIAS_ASSERT_MSG(mHasValue, "Promise value not ready!");
        rethrow_if_needed();

        // Get value
        mHasValue = false;
        return std::move(mStorage.value); 
    }
    Task<T> get_return_object() noexcept {
        auto handle = handle_type::from_promise(*this);
        this->mHandle = handle;
        return Task<T>::from(handle, false);
    }
protected:
    union Storage {
        Storage() { }
        ~Storage() { }

        Result<T> value;
        uint8_t _pad[sizeof(Result<T>)];
    } mStorage;
    bool mHasValue = false; //< Has value
};

/**
 * @brief A Refcounted Promise Pointer
 * 
 */
class PromiseRef {
public:
    PromiseRef() = default;
    template <typename T>
    PromiseRef(std::coroutine_handle<Promise<T> > handle) : mPtr(&handle.promise()) {
        _ref();
    }
    PromiseRef(const PromiseRef &h) : mPtr(h.mPtr) {
        _ref();
    }
    PromiseRef(PromiseRef &&h) : mPtr(h.mPtr) {
        h.mPtr = nullptr;
    }
    ~PromiseRef() {
        _deref();
    }
    PromiseBase *get() const noexcept {
        return mPtr;
    }
    PromiseBase &promise() const noexcept {
        return *mPtr;
    }
    PromiseRef &operator =(const PromiseRef &ref) {
        if (this == &ref) {
            return *this;
        }
        _deref();
        mPtr = ref.mPtr;
        _ref();
        return *this;
    }
    PromiseRef &operator =(PromiseRef &&ref) {
        if (this == &ref) {
            return *this;
        }
        _deref();
        mPtr = ref.mPtr;
        ref.mPtr = nullptr;
        return *this;
    }
    PromiseRef &operator =(std::nullptr_t) {
        _deref();
        return *this;
    } 
    PromiseBase *operator ->() const noexcept {
        return mPtr;
    }
protected:
    void _ref() {
        if (mPtr) {
            mPtr->ref();
        }
    }
    void _deref() {
        if (mPtr) {
            mPtr->deref();
            mPtr = nullptr;
        }
    }
    PromiseBase *mPtr = nullptr;
};

/**
 * @brief A Handle used to resume task
 * 
 */
class ResumeHandle {
public:
    template <typename T>
    ResumeHandle(std::coroutine_handle<Promise<T> > handle) : mRef(handle) {
        mRef->set_suspend_by_await(true);
    }
    ResumeHandle(const ResumeHandle &) = default;
    ResumeHandle() = default;

    void resume() noexcept {
        ILIAS_ASSERT(mRef.get());
        mRef->resume_later();
        mRef = nullptr;
    }
    void operator ()() noexcept {
        resume();
    }
    PromiseBase &promise() const noexcept {
        return mRef.promise();
    }
    bool isAborted() const noexcept {
        return mRef->aborted();
    }
    explicit operator bool() const noexcept {
        return mRef.get();
    }
private:
    PromiseRef mRef;
};
/**
 * @brief A handle to observe the task, it can be abort the task
 * 
 */
class AbortHandle {
public:
    template <typename T>
    explicit AbortHandle(std::coroutine_handle<Promise<T> > handle) : mRef(handle) { }
    AbortHandle(const AbortHandle &) = default;
    ~AbortHandle() = default;

    void abort() {
        ILIAS_ASSERT(mRef.get());
        return mRef->abort();
    }
    bool done() const noexcept {
        ILIAS_ASSERT(mRef.get());
        return mRef->handle().done();
    }
    bool isAborted() const noexcept {
        ILIAS_ASSERT(mRef.get());
        return mRef->aborted();
    }
    explicit operator bool() const noexcept {
        return mRef.get();
    }
private:
    PromiseRef mRef;
};
/**
 * @brief A handle to observe the task, it can be abort, join the task
 * 
 */
template <typename T>
class JoinHandle {
public:
    explicit JoinHandle(std::coroutine_handle<Promise<T> > handle) : mRef(handle) { }
    JoinHandle(const JoinHandle &) = default;
    ~JoinHandle() = default;

    void abort() {
        ILIAS_ASSERT(mRef.get());
        return mRef->abort();
    }
    bool done() const noexcept {
        ILIAS_ASSERT(mRef.get());
        return mRef->handle().done();
    }
    bool isAborted() const noexcept {
        ILIAS_ASSERT(mRef.get());
        return mRef->aborted();
    }
    explicit operator bool() const noexcept {
        return mRef.get();
    }
private:
    PromiseRef mRef;
};

/**
 * @brief Join a coroutine
 * 
 * @tparam T 
 */
template <typename T>
class JoinAwaitable {
public:
    JoinAwaitable(Task<T> &&t) : mTask(std::move(t)) { }
    JoinAwaitable(const JoinAwaitable &) = delete;
    JoinAwaitable(JoinAwaitable &&) = default;
    ~JoinAwaitable() = default;

#if defined(ILIAS_COROUTINE_TRACE) && !defined(ILIAS_COROUTINE_NO_AWAIT_TRACE)
    bool await_ready() const noexcept {
        return false;
    }
    bool await_suspend(const ResumeHandle &h) noexcept {
        mName = h.promise().name();
        ::fprintf(stderr, "[Ilias] co '%s' was try to await '%s'\n", mName, mTask.promise().name());
        if (_await_ready()) {
            // Resume co
            return false;
        }
        ::fprintf(stderr, "[Ilias] co '%s' was suspended by '%s'\n", mName, mTask.promise().name());
        _await_suspend(h);
        return true;
    }
    Result<T> await_resume() const {
        ::fprintf(stderr, "[Ilias] co '%s' was resumed\n", mName);
        return _await_resume();
    }
private:
    const char *mName = nullptr;
#else
    bool await_ready() const noexcept {
        return _await_ready();
    }
    void await_suspend(const ResumeHandle &h) noexcept {
        return _await_suspend(h);
    }
    Result<T> await_resume() const {
        return _await_resume();
    }
#endif

private:
    bool _await_ready() const noexcept {
        // Let it run true on done, false on await
        // So on true, this awaitable is ready
        return mTask.promise().resume_util_done_or_await();
    }
    void _await_suspend(const ResumeHandle &h) noexcept {
        mSelf = h;
        mTask.promise().set_await_promise(h.promise());

        // Run mHandle at EventLoop
        mTask.promise().resume_later();
    }
    Result<T> _await_resume() const {
        if (mSelf && mSelf.isAborted()) {
            mTask.promise().abort(); //< Abort current awaiting task
            return Unexpected(Error::Aborted);
        }
        return mTask.get();
    }

    Task<T> mTask;
    ResumeHandle mSelf;
};
/**
 * @brief Wrapping any awaitable, for support abort
 * 
 * @tparam T 
 */
template <typename T>
class AwaitWrapper {
public:
    AwaitWrapper(const PromiseBase *p, T &&awaitable) : mPtr(p), mAwait(std::move(awaitable)) { }

    bool await_ready() {
        mAborted = mPtr->aborted();
        if (mAborted) {
            return true;
        }
        return mAwait.await_ready();
    }
    template <typename U>
    auto await_suspend(std::coroutine_handle<Promise<U> > handle) {
        return mAwait.await_suspend(handle);
    }
    auto await_resume() -> AwaitableResult<T> {
        if (mAborted) {
            return Unexpected(Error::Aborted);
        }
        return mAwait.await_resume();
    }
private:
    const PromiseBase *mPtr;
    T mAwait;
    bool mAborted = false; //< Before we called wrapped awaitable, current task is already aborted
};

// --- AwaitTransform Impl for Task<T>
template <typename T>
class AwaitTransform<Task<T> > {
public:
    JoinAwaitable<T> transform(Task<T> &&t) const noexcept {
        return JoinAwaitable<T>(std::move(t));
    }
};

// --- EventLoop Impl
inline EventLoop::EventLoop() {
    ILIAS_ASSERT(instance() == nullptr);
    setInstance(this);
}
inline EventLoop::~EventLoop() {
    ILIAS_ASSERT(instance() == this);
    setInstance(nullptr);
}
inline EventLoop *EventLoop::instance() noexcept {
    return _tls().loop;
}
inline EventLoop *EventLoop::setInstance(EventLoop *loop) noexcept {
    auto prev = instance();
    _tls().loop = loop;
    return prev;
}
inline EventLoop::Tls &EventLoop::_tls() noexcept {
    static thread_local Tls tls;
    return tls;
}

template <typename Callable, typename ...Args>
inline AbortHandle EventLoop::spawn(Callable &&cb, Args &&...args) {
    return postTask(std::invoke(std::forward<Callable>(cb), std::forward<Args>(args)...));
}

template <typename T>
inline Result<T> EventLoop::runTask(const Task<T> &task) {
    task.promise().set_quit_at_done(true);
    postCoroutine(task.promise());
    run(); //< Enter event loop
    return task.get(); //< Get result value
}

template <typename T>
inline AbortHandle EventLoop::postTask(Task<T> &&task) {
    using handle_type = typename Task<T>::handle_type;
    handle_type handle = task.handle(); //< Release the coroutinue ownship
    postCoroutine(handle.promise());
    return AbortHandle(handle);
}
inline void EventLoop::postCoroutine(PromiseBase &promise) {
    promise.ref();
    post(_invokeCoroutine, &promise);
}
inline void EventLoop::_invokeCoroutine(void *ptr) {
    auto &promise = *static_cast<PromiseBase *>(ptr);

    promise.resume_util_done_or_await();
    promise.set_suspend_by_await(false); //< restore value
    promise.deref();
}

inline void EventLoop::quit() {
    instance()->requestStop();
}

// --- Utils Function
inline Task<> msleep(int64_t ms) noexcept {
    struct Awaitable {
        bool await_ready() const noexcept {
            return ms <= 0;
        }
        void await_suspend(ResumeHandle &&handle) noexcept {
            resumeHandle = std::move(handle);
            timerid = EventLoop::instance()->addTimer(ms, [](void *resume) {
                auto a = static_cast<Awaitable*>(resume);
                a->timerid = 0;
                a->resumeHandle();
            }, this, EventLoop::TimerSingleShot);
        }
        Result<> await_resume() const noexcept {
            if (timerid != 0) {
                EventLoop::instance()->delTimer(timerid);
                return Unexpected(Error::Aborted);
            }
            return Result<>();
        }
        int64_t ms = 0;
        uintptr_t timerid = 0;
        ResumeHandle resumeHandle;
    };
    co_return co_await Awaitable {ms};
}

ILIAS_NS_END