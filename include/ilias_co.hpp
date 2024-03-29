#pragma once

#include <cstdio>
#include <version>
#include <concepts>
#include <coroutine>
#include <exception>
#include <functional>
#include "ilias.hpp"
#include "ilias_source_location.hpp"

#if !defined(__cpp_lib_coroutine)
#error "Compiler does not support coroutines"
#endif

#if  defined(ILIAS_NO_SOURCE_LOCATION)
#undef ILIAS_COROUTINE_TRACE
#endif

ILIAS_NS_BEGIN

// --- Function
#if defined(__cpp_lib_move_only_function)
template <typename ...Args>
using Function = std::move_only_function<Args...>;
#else
template <typename ...Args>
using Function = std::function<Args...>;
#endif

// --- Coroutine 
template <typename T = void>
class Task;
template <typename T = void>
class Promise;
class PromiseBase;
template <typename T>
class AwaitTransform;
template <typename T>
class IAwaitable;

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
    T runTask(const Task<T> &task);

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

// --- Task Impl
template <typename T>
class Task {
public:
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<Promise<T> >;

    Task() = default;
    Task(const Task &t) : mHandle(t.mHandle) { if (mHandle) mHandle.promise().ref(); }
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

    T get() const {
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

// --- Allocate Impl
class AllocBase {
public:
    void *operator new(size_t n) {
        return ILIAS_MALLOC(n);
    }
    void operator delete(void *p) noexcept {
        return ILIAS_FREE(p);
    }
protected:
    AllocBase() = default;
    ~AllocBase() = default;
};

// --- Promise Impl
class PromiseBase : public AllocBase {
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
        return AwaitTransform<T>().transform(std::forward<T>(t));
    }
    /**
     * @brief Passthrough awaitable
     * 
     * @tparam T must be Awaitable
     * @param t 
     * @return T 
     */
    template <Awaitable T>
    T await_transform(T &&t) const noexcept {
        return std::forward<T>(t);
    }
#if defined(ILIAS_COROUTINE_TRACE)
    void unhandled_exception() noexcept {
        mException = std::current_exception();
        try {
            std::rethrow_exception(mException);
        } 
        catch (const std::exception &e) {
            ::fprintf(stderr, "[Ilias] co '%s' %s was thrown what(): %s\n", mName, typeid(e).name(), e.what());
        } 
        catch (...) {
            ::fprintf(stderr, "[Ilias] co '%s' exception was thrown\n", mName);
        }
    }
#else
    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }
#endif
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
    int  mRefcount = 1; //< Inited refcount
    const char *mName = nullptr; //< Name for debug track
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
        this->mName= loc.function_name();
        ::fprintf(stderr, "[Ilias] co '%s' was created\n", this->name());
    }
    ~Promise() {
        ::fprintf(stderr, "[Ilias] co '%s' was destroyed\n", this->name());
    }
#else
    Promise() = default;
    ~Promise() = default;
#endif

    Task<T> get_return_object() noexcept {
        auto handle = handle_type::from_promise(*this);
        this->mHandle = handle;
        return Task<T>::from(handle, false);
    }
};

// --- ResumeHandle
class ResumeHandle : public AllocBase {
public:
    ResumeHandle() { }
    template <typename T>
    ResumeHandle(std::coroutine_handle<Promise<T> > handle) : mPtr(&handle.promise()) {
        _ref();
        mPtr->set_suspend_by_await(true);
    }
    ResumeHandle(const ResumeHandle &h) : mPtr(h.mPtr) {
        _ref();
    }
    ResumeHandle(ResumeHandle &&h) : mPtr(h.mPtr) {
        h.mPtr = nullptr;
    }
    ~ResumeHandle() {
        _deref();
    }

    void resume() noexcept {
        ILIAS_ASSERT(mPtr);
        mPtr->resume_later();
        mPtr->deref();
        mPtr = nullptr;
    }
    void operator ()() noexcept {
        resume();
    }
    PromiseBase *get() const noexcept {
        return mPtr;
    }
    PromiseBase &promise() const noexcept {
        return *mPtr;
    }
    ResumeHandle &operator =(const ResumeHandle &ref) {
        if (this == &ref) {
            return *this;
        }
        _deref();
        mPtr = ref.mPtr;
        _ref();
        return *this;
    }
    ResumeHandle &operator =(ResumeHandle &&ref) {
        if (this == &ref) {
            return *this;
        }
        _deref();
        mPtr = ref.mPtr;
        ref.mPtr = nullptr;
        return *this;
    }
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
    PromiseBase *mPtr = nullptr;
    void        *mUser = nullptr;
};

// --- TaskAwaitable Impl
template <typename T>
class TaskAwaitable {
public:
    TaskAwaitable(Task<T> &&t) : mTask(std::move(t)) { }
    TaskAwaitable(const Task<T> &t) : mTask(t) { }
    TaskAwaitable(const TaskAwaitable &) = delete;
    TaskAwaitable(TaskAwaitable &&) = default;
    ~TaskAwaitable() = default;

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
    T await_resume() const {
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
    T await_resume() const {
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
        mTask.promise().set_await_promise(h.promise());

        // Run mHandle at EventLoop
        mTask.promise().resume_later();
    }
    T _await_resume() const {
        return mTask.get();
    }

    Task<T> mTask;
};

/**
 * @brief Helper for make Awaitable as dynamic type
 * 
 * @tparam T 
 */
template <typename T = void>
class IAwaitable {
public:
    IAwaitable() = default;
    IAwaitable(const IAwaitable &) = delete;
    IAwaitable(IAwaitable &&other) : mPtr(other.mPtr) { other.mPtr = nullptr; }
    ~IAwaitable() { delete mPtr; }

    template <Awaitable U>
    IAwaitable(U &&other) : mPtr(new Impl<U>(std::move(other))) { }
    template <AwaitTransformable U>
    IAwaitable(U &&other) : IAwaitable(AwaitTransform<U>().transform(std::move(other))) { }

    bool await_ready() const {
        return mPtr->await_ready();
    }
    bool await_suspend(ResumeHandle &&h) const {
        return mPtr->await_suspend(std::move(h));
    }
    T await_resume() const {
        return mPtr->await_resume();
    }

    template <typename U>
    U &view() {
        ILIAS_ASSERT_MSG(dynamic_cast<Impl<U> *>(mPtr) != nullptr, "Invalid type");
        return static_cast<Impl<U> *>(mPtr)->value;
    }
private:
    struct Base {
        virtual ~Base() = default;
        virtual bool await_ready() = 0;
        virtual bool await_suspend(ResumeHandle &&handle) = 0;
        virtual T await_resume() = 0;
    };
    template <typename U>
    struct Impl final : public Base {
        Impl(U &&value) : value(std::move(value)) { }
        Impl(const Impl &) = delete;
        ~Impl() = default;
        bool await_ready() override {
            return value.await_ready();
        }
        bool await_suspend(ResumeHandle &&handle) override {
            using RetT = decltype(value.await_suspend(std::move(handle)));
            if constexpr(std::is_same_v<RetT, void>) {
                value.await_suspend(std::move(handle));
                return true;
            }
            else {
                return value.await_suspend(std::move(handle));
            }
        }
        T await_resume() override {
            return value.await_resume();
        }
        U value;
    };

    Base *mPtr = nullptr;
};

template <Awaitable T>
IAwaitable(T &&u) -> IAwaitable<decltype(std::declval<T>().await_resume())>;
template <AwaitTransformable T>
IAwaitable(T &&u) -> IAwaitable<decltype(std::declval<decltype(AwaitTransform<T>().transform(T()))>().await_resume())>;

/**
 * @brief Helper for wrapping callback into awaitable
 * 
 * @tparam T 
 */
template <typename T = void>
class CallbackAwaitable {
public:
    struct ResumeFunc {
        using Type = std::conditional_t<std::is_trivial_v<T> && std::is_standard_layout_v<T>, T, T &&>;
        void operator ()(Type t) noexcept {
            awaitable->_put(std::forward<T>(t));
            handle.resume();
        }
        CallbackAwaitable *awaitable;
        ResumeHandle handle;
    };
    using SuspendFunc = Function<void(ResumeFunc &&)>;

    CallbackAwaitable(SuspendFunc &&func) : mFunc(std::move(func)) { }
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
    void await_suspend(ResumeHandle &&handle) noexcept {
        ResumeFunc func;
        func.awaitable = this;
        func.handle = handle;
        mFunc(std::move(func));
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
    void _put(T &&value) {
        new (&mStorage.value) T(std::move(value));
        mHasValue = true;
    }

    union Storage {
        Storage() { }
        Storage(Storage &&) { }
        Storage(const Storage &) { }
        ~Storage() { }

        T value;
        uint8_t _pad[sizeof(T)];
    } mStorage;
    bool mHasValue = false;
    SuspendFunc mFunc;
};
template <>
class CallbackAwaitable<void> {
public:
    using ResumeFunc = ResumeHandle;
    using SuspendFunc = Function<void(ResumeFunc &&)>;

    CallbackAwaitable(SuspendFunc &&func) : mFunc(std::move(func)) { }
    CallbackAwaitable(const CallbackAwaitable &) = delete;
    CallbackAwaitable(CallbackAwaitable &&) = default;

    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(ResumeHandle resumeHandle) noexcept {
        mFunc(std::move(resumeHandle));
    }
    void await_resume() const noexcept { }
private:
    SuspendFunc mFunc;
};

// --- AwaitTransform Impl for Task<T>
template <typename T>
class AwaitTransform<Task<T> > {
public:
    TaskAwaitable<T> transform(const Task<T> &t) const noexcept {
        return TaskAwaitable<T>(t);
    }
    TaskAwaitable<T> transform(Task<T> &&t) const noexcept {
        return TaskAwaitable<T>(std::move(t));
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
inline void EventLoop::spawn(Callable &&cb, Args &&...args) {
    postTask(std::invoke(std::forward<Callable>(cb), std::forward<Args>(args)...));
}

template <typename T>
inline T EventLoop::runTask(const Task<T> &task) {
    task.promise().set_quit_at_done(true);
    postCoroutine(task.promise());
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
inline auto msleep(int64_t ms) noexcept {
    struct Awaitable {
        bool await_ready() const noexcept {
            return ms <= 0;
        }
        void await_suspend(ResumeHandle &&handle) noexcept {
            resumeHandle = std::move(handle);
            EventLoop::instance()->addTimer(ms, [](void *resume) {
                auto a = static_cast<Awaitable*>(resume);
                a->resumeHandle();
            }, this, EventLoop::TimerSingleShot);
        }
        void await_resume() const noexcept {

        }
        int64_t ms;
        ResumeHandle resumeHandle;
    };
    return Awaitable {ms};
}

ILIAS_NS_END