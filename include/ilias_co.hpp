#pragma once

#include <cstdio>
#include <version>
#include <coroutine>
#include <exception>
#include <functional>
#include "ilias.hpp"
#include "ilias_source_location.hpp"

#if !defined(__cpp_lib_coroutine)
#error "Compiler does not support coroutines"
#endif

#if !defined(ILIAS_NO_SOURCE_LOCATION) && !defined(NDEBUG)
#define ILIAS_COROUTINE_TRACE
#endif

ILIAS_NS_BEGIN

// --- Coroutine 
template <typename T = void>
class Task;
template <typename T = void>
class Promise;
class PromiseBase;
template <typename T>
class TaskAwaitable;

/**
 * @brief Abstraction of event loop
 * 
 */
class EventLoop {
public:
    virtual void requestStop() = 0;
    virtual void run() = 0;
    virtual void post(void (*fn)(void *), void *arg = nullptr) = 0;
    virtual void timerSingleShot(int64_t ms, void (*fn)(void *), void *arg) = 0;

    /**
     * @brief Spawn a task add post it
     * 
     * @tparam Callable 
     * @tparam Args 
     * @param callable 
     * @param args 
     */
    template <typename Callable, typename ...Args>
    void spawn(Callable &&callable, Args &&...args);
    /**
     * @brief Post a task
     * 
     * @tparam T 
     * @param task 
     */
    template <typename T>
    void postTask(const Task<T> &task);
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
     * @return T 
     */
    template <typename T>
    T runTask(Task<T> task);

    static void quit();
    static EventLoop *&instance() noexcept;
private:
    static void _invokeCoroutine(void *promiseBase);
protected:
    EventLoop();
    EventLoop(const EventLoop &) = delete;
    ~EventLoop();
};

// --- Task Impl
template <typename T>
class Task {
public:
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<Promise<T> >;

    explicit Task(handle_type handle) : mHandle(handle) { }
    Task() = default;
    Task(const Task&) = delete;
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

    T get() const {
        return promise().value();
    }
private:
    handle_type mHandle;
};

// --- Promise Impl
class PromiseBase {
public:
    PromiseBase() {

    }
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
            mEventLoop->postCoroutine(*mAwaitPromise);
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
     * @brief Transform Task into awaitable
     * 
     * @tparam T 
     * @param t 
     * @return TaskAwaitable<T> 
     */
    template <typename T>
    TaskAwaitable<T> await_transform(Task<T> &&t) const noexcept {
        return TaskAwaitable(std::move(t));
    }
    /**
     * @brief Passthrough another things
     * 
     * @tparam T 
     * @param t 
     * @return T 
     */
    template <typename T>
    T await_transform(T &&t) const noexcept {
        return std::move(t);
    }
    void unhandled_exception() noexcept {
        mException = std::current_exception();
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
    void rethrow_if_needed() {
        if (mException) {
            std::rethrow_exception(std::move(mException));
        }
    }
    void ref() noexcept {
        ++mRefcount;
    }
    void deref() noexcept {
        if (--mRefcount == 0) {
            mHandle.destroy();
        }
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
    template <typename T>
    void set_await_handle(std::coroutine_handle<Promise<T> > h) {
        mAwaitPromise = &h.promise();
        mAwaitPromise->ref();
    }
    void set_event_loop(EventLoop *event_loop) noexcept {
        mEventLoop = event_loop;
    }
    EventLoop *event_loop() const noexcept {
        return mEventLoop;
    }
    std::coroutine_handle<> handle() const noexcept {
        return mHandle;
    }

    void *operator new(size_t n) {
        return ILIAS_MALLOC(n);
    }
    void operator delete(void *p) noexcept {
        return ILIAS_FREE(p);
    }
protected:
    bool mSuspendByAwait = false;
    bool mQuitAtDone = false; //< Quit the event loop if coroutinue is doned
    int  mRefcount = 1; //< Inited refcount
    EventLoop *mEventLoop = EventLoop::instance();
    std::exception_ptr mException;
    std::coroutine_handle<> mHandle; //< A Handle pointer to self handle
    PromiseBase            *mAwaitPromise = nullptr; //< Another coroutine await you
};

template <typename T>
class PromiseImpl : public PromiseBase {
public:
    PromiseImpl() {

    }
    ~PromiseImpl() {
        if (mDestruct) {
            mStorage.value.~T();
        }
    }

    template <typename U>
    void return_value(U &&value) noexcept {
        mHasValue = true;
        mDestruct = true;

        new (&mStorage.value) T(std::forward<U>(value));
    }
    bool has_value() const noexcept {
        return mHasValue;
    }
    /**
     * @brief Get current value
     * 
     * @return T 
     */
    T value() {
        ILIAS_ASSERT_MSG(mHasValue, "Promise value not ready!");
        rethrow_if_needed();

        // Get value
        mHasValue = false;
        return std::move(mStorage.value); 
    }
protected:
    union Storage {
        Storage() { }
        ~Storage() { }

        T value;
        uint8_t _pad[sizeof(T)];
    } mStorage;

    bool mDestruct = false; //< Need call destructor 
    bool mHasValue = false; //< Has value
};


template <>
class PromiseImpl<void> : public PromiseBase {
public:
    void return_void() noexcept {

    }
    void value() {
        rethrow_if_needed();
    }
};

template <typename T>
class Promise final : public PromiseImpl<T> {
public:
    using handle_type = std::coroutine_handle<Promise<T> >;

#if defined(ILIAS_COROUTINE_TRACE) && !defined(ILIAS_COROUTINE_NO_CREATE_TRACE)
    Promise(std::source_location loc = std::source_location::current()) {
        mLocation = loc;
        ::fprintf(stderr, "[Ilias] co '%s' was created\n", name());
    }
    ~Promise() {
        ::fprintf(stderr, "[Ilias] co '%s' was destroyed\n", name());
    }
    const char *name() const noexcept { //< Debug name
        return mLocation.function_name();
    }
private:
    std::source_location mLocation;
#else
    Promise() = default;
    ~Promise() = default;
#endif

public:
    Task<T> get_return_object() noexcept {
        auto handle = handle_type::from_promise(*this);
        this->mHandle = handle;
        return Task<T>(handle);
    }
};

// --- PromiseRef Impl
template <typename T>
class PromiseRef {
public:
    PromiseRef(Promise<T> *promise) : mPtr(promise) {
        _ref();
    }
    PromiseRef(const PromiseRef &ref) : mPtr(ref.mPtr) {
        _ref();
    }
    ~PromiseRef() {
        _deref();
    }

    Promise<T> &operator *() const noexcept {
        return *mPtr;
    }
    Promise<T> *operator ->() const noexcept {
        return mPtr;
    }

    PromiseRef &operator =(const PromiseRef &ref) = delete;
    PromiseRef &operator =(PromiseRef &&ref) = delete;
private:
    void _ref() {
        if (mPtr) {
            mPtr->ref();
        }
    }
    void _deref() {
        if (mPtr) {
            mPtr->deref();
        }
    }
    Promise<T> *mPtr = nullptr;
};

// --- TaskAwaitable Impl
template <typename T>
class TaskAwaitable {
public:
    TaskAwaitable(Task<T> &&t) : mTask(std::move(t)) {}
    TaskAwaitable(const TaskAwaitable &) = delete;
    TaskAwaitable(TaskAwaitable &&) = default;
    ~TaskAwaitable() = default;

#if defined(ILIAS_COROUTINE_TRACE) && !defined(ILIAS_COROUTINE_NO_AWAIT_TRACE)
    bool await_ready(std::source_location loc = std::source_location::current()) noexcept {
        mLocation = loc;
        ::fprintf(stderr, "[Ilias] co '%s' was try to await '%s'\n", loc.function_name(), mTask.promise().name());
        return _await_ready();
    }
    template <typename U>
    void await_suspend(std::coroutine_handle<Promise<U> > h) noexcept {
        ::fprintf(stderr, "[Ilias] co '%s' was suspended by '%s'\n", mLocation.function_name(), mTask.promise().name());
        return _await_suspend(h);
    }
    T await_resume() const noexcept {
        ::fprintf(stderr, "[Ilias] co '%s' was resumed\n", mLocation.function_name());
        return _await_resume();
    }
private:
    std::source_location mLocation;
#else
    bool await_ready() const noexcept {
        return _await_ready();
    }
    template <typename U>
    void await_suspend(std::coroutine_handle<Promise<U> > h) noexcept {
        return _await_suspend(h);
    }
    T await_resume() const noexcept {
        return _await_resume();
    }
#endif

private:
    bool _await_ready() const noexcept {
        // Let it run true on done, false on await
        // So on true, this awaitable is ready
        return mTask.promise().resume_util_done_or_await();
    }
    template <typename U>
    void _await_suspend(std::coroutine_handle<Promise<U> > h) noexcept {
        h.promise().set_suspend_by_await(true);
        mTask.promise().set_await_handle(h);

        // Run mHandle at EventLoop
        mTask.promise().event_loop()->postTask(mTask);
    }
    T _await_resume() const noexcept {
        return mTask.get();
    }

    Task<T> mTask;
};

// --- CallbackAwaitable Impl
// Helper for wrapper callback
template <typename T = void>
class CallbackAwaitable {
public:
    using ResumeFunc = std::function<void(T &&)>;
    using SuspendFunc = std::function<void(ResumeFunc &&)>;

    CallbackAwaitable(SuspendFunc &&func) : mFunc(func) { }
    CallbackAwaitable(const CallbackAwaitable &) = delete;
    CallbackAwaitable(CallbackAwaitable &&) = default;
    ~CallbackAwaitable() {
        if (mHasValue) { 
            mStorage.value.~T();
        }
    }

    bool await_ready() const noexcept {
        return false;
    }
    template <typename U>
    void await_suspend(std::coroutine_handle<Promise<U> > h) noexcept {
        Promise<U> *promise = &h.promise();
        promise->set_suspend_by_await(true);
        mFunc([p = PromiseRef(promise), this](T &&v) {
            _resume(*p, std::move(v));
        });
    }
    T await_resume() noexcept {
        ILIAS_ASSERT(mHasValue);
        struct Guard {
            ~Guard() {
                self->mStorage.value.~T();
            }
            CallbackAwaitable *self;
        };
        Guard guard{this};
        return std::move(mStorage.value);
    }
private:
    void _resume(PromiseBase &promise, T &&value) {
        new (&mStorage.value) T(std::move(value));
        mHasValue = true;
        promise.event_loop()->postCoroutine(promise);
    }

    union Storage {
        T value;
        uint8_t _pad[sizeof(T)];
    } mStorage;
    bool mHasValue = false;
    SuspendFunc mFunc;
};
template <>
class CallbackAwaitable<void> {
public:
    using ResumeFunc = std::function<void()>;
    using SuspendFunc = std::function<void(ResumeFunc &&)>;

    CallbackAwaitable(SuspendFunc &&func) : mFunc(func) { }
    CallbackAwaitable(const CallbackAwaitable &) = delete;
    CallbackAwaitable(CallbackAwaitable &&) = default;

    bool await_ready() const noexcept {
        return false;
    }
    template <typename U>
    void await_suspend(std::coroutine_handle<Promise<U> > h) noexcept {
        Promise<U> *promise = &h.promise();
        promise->set_suspend_by_await(true);
        mFunc([loop = EventLoop::instance(), p = PromiseRef(promise)]() {
            loop->postCoroutine(*p);
        });
    }
    void await_resume() const noexcept {}
private:
    SuspendFunc mFunc;
};

//--- GetPromiseAwaitable Impl
// Helper for get PromsieBase in co env
class GetPromiseAwaitable {
public:
    bool await_ready() const noexcept {
        return false;
    }
    template <typename T>
    bool await_suspend(std::coroutine_handle<T> h) noexcept {
        T *promise = &h.promise();
        mPromise = promise;
        return false;
    }
    PromiseBase *await_resume() const noexcept {
        return mPromise;   
    }
private:
    PromiseBase *mPromise = nullptr;
};

// --- EventLoop Impl
inline EventLoop::EventLoop() {
    ILIAS_ASSERT(instance() == nullptr);
    instance() = this;
}
inline EventLoop::~EventLoop() {
    ILIAS_ASSERT(instance() == this);
    instance() = nullptr;
}
inline EventLoop *&EventLoop::instance() noexcept {
#ifdef ILIAS_EVENTLOOP_INSTANCE
    return ILIAS_EVENTLOOP_INSTANCE;
#else
    static thread_local EventLoop *eventLoop = nullptr;
    return eventLoop;
#endif
}

template <typename Callable, typename ...Args>
inline void EventLoop::spawn(Callable &&cb, Args &&...args) {
    postTask(std::invoke(std::forward<Callable>(cb), std::forward<Args>(args)...));
}

template <typename T>
inline T EventLoop::runTask(Task<T> task) {
    task.promise().set_quit_at_done(true);
    post(_invokeCoroutine, &task.promise());
    run(); //< Enter event loop
    return task.get(); //< Get result value
}

template <typename T>
inline void EventLoop::postTask(const Task<T> &task) {
    using handle_type = typename Task<T>::handle_type;
    handle_type handle = task.handle(); //< Release the coroutinue ownship
    postCoroutine(handle.promise());
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
inline CallbackAwaitable<> msleep(int64_t ms) {
    return CallbackAwaitable<>([ms](CallbackAwaitable<>::ResumeFunc &&func) {
        if (ms == 0) {
            func();
            return;
        }
        EventLoop::instance()->timerSingleShot(ms, [](void *ptr) {
            auto func = static_cast<CallbackAwaitable<>::ResumeFunc*>(ptr);
            (*func)();
            delete func;
        }, new CallbackAwaitable<>::ResumeFunc(std::move(func)));
    });
}

ILIAS_NS_END