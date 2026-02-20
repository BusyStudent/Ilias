#include <ilias/defines.hpp>

#if defined(ILIAS_USE_FIBER)
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/fiber.hpp> // Fiber

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp> // CreateFiber
#elif __has_include(<ucontext.h>)
    #include "linux/libucontext.hpp" // sys::getcontext, sys::makecontext
    #include <sys/mman.h> // mmap
    #include <unistd.h> // sysconf
#else
    #error "No fiber support on this platform"
#endif // _WIN32

#if !defined(__cpp_exceptions)
    #error "Fiber need to use exceptions to unwind the stack"
#endif

ILIAS_NS_BEGIN

namespace fiber {
namespace {

// MARK: Impl
// The hidden implment of the fiber
class FiberContextImpl : public FiberContext {
public:
    // Guard magic
    uint32_t mMagic = 0x114514;

    // TODO: TRACING
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    runtime::CaptureSource mSuspendPoint; // The source location of the fiber suspend
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    runtime::CaptureSource mCreation; // The source location of the fiber create

    // State
    runtime::StopSource mStopSource;
    runtime::Executor *mExecutor = nullptr;
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

    // Internal state
    bool mRunning = false;
    bool mStarted = false;

    // Platform data
#if defined(_WIN32)
    struct win32 {
        ::HANDLE handle = nullptr;
        ::HANDLE caller = nullptr;
    } win32;
#else
    struct {
        sys::ucontext_t caller {};
        sys::ucontext_t self {};
        void           *mmapPtr = nullptr;
        size_t          mmapSize = 0;
    } posix;
#endif // _WIN32

    // Internal Method
    auto main() -> void;
    auto suspend() -> void;
    auto schedule() -> void;

    // Impl of the user interface
    auto resumeImpl() -> bool;
    auto destroyImpl() -> void;

    static auto current() -> FiberContextImpl *;
};

#if !defined(_WIN32) // Not in windows, we should save it by ourselves
static constinit thread_local FiberContextImpl *currentContext {};

struct CurrentGuard { // RAII guard for manage the current fiber
    CurrentGuard(FiberContextImpl *c) : cur(c) {
        prev = std::exchange(currentContext, cur);
    }
    ~CurrentGuard() {
        currentContext = prev;
    }
    FiberContextImpl *cur = nullptr;
    FiberContextImpl *prev = nullptr;
};
#endif // _WIN32

auto FiberContextImpl::main() -> void {
    mStarted = true;
    mRunning = true;

    // Call the entry
    try {
        mValue = mEntry.invoke(mEntry.args);
    }
    catch (FiberCancellation &) {
        mStopped = true;
    }
    catch (...) { // Another user exception
        mException = std::current_exception();
    }
    mComplete = true;
    mRunning = false;

    // Notify
    if (mCompletionHandler) {
        mCompletionHandler(this, mUser);
    }
}

auto FiberContextImpl::destroyImpl() -> void {
    ILIAS_ASSERT(!mRunning, "Cannot destroy a running fiber");
    ILIAS_ASSERT(!(mStarted && !mComplete), "Cannot destroy a suspended fiber");

    // Ok, safe to destroy
#if defined(_WIN32)
    ::DeleteFiber(win32.handle);
#else
    ::munmap(posix.mmapPtr, posix.mmapSize);
#endif // _WIN32

    // Destroy the entry
    if (mEntry.cleanup) {
        mEntry.cleanup(mEntry.args);
    }
    delete this;
}

auto FiberContextImpl::resumeImpl() -> bool {
    ILIAS_ASSERT(!mRunning && !mComplete, "Cannot resume a running or complete fiber");

#if defined(_WIN32)
    struct ConvertGuard {
        ConvertGuard() { // The main fiber may use float point, save it
            if (::IsThreadAFiber()) {
                return;
            }
            if (auto ret = ::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH); !ret) {
                ILIAS_THROW(std::system_error(std::error_code(GetLastError(), std::system_category()), "Faliled to convert thread to fiber"));
            }
            converted = true;
        }
        ~ConvertGuard() {
            if (converted) {
                auto ok = ::ConvertFiberToThread();
                ILIAS_ASSERT(ok);
            }
        }

        bool converted = false;
    };
    static thread_local ConvertGuard guard;
    win32.caller = ::GetCurrentFiber();
    ::SwitchToFiber(win32.handle);
#else
    CurrentGuard guard {this};
    sys::swapcontext(&posix.caller, &posix.self);
#endif // _WIN32

    return mComplete;
}

auto FiberContextImpl::suspend() -> void {
    ILIAS_ASSERT(mRunning && !mComplete, "Cannot suspend a non-running or complete fiber");
    mRunning = false;

    // Switch back to the resume point
#if defined(_WIN32)
    auto caller = std::exchange(win32.caller, nullptr);
    ::SwitchToFiber(caller);
#else
    sys::swapcontext(&posix.self, &posix.caller);
#endif // _WIN32

    mRunning = true;
}

auto FiberContextImpl::schedule() -> void {
    mExecutor->post([](void *ptr) {
        static_cast<FiberContextImpl *>(ptr)->resumeImpl();
    }, this);
}

auto FiberContextImpl::current() -> FiberContextImpl * {

#if defined(_WIN32)
    auto data = static_cast<FiberContextImpl *>(::GetFiberData());
    ILIAS_ASSERT(data->mMagic == 0x114514, "Magic number mismatch, memory corrupted ???");
    return data;
#else
    return currentContext;
#endif // _WIN32

}

auto callContext(void *ctxt) -> void {
    auto self = static_cast<FiberContextImpl *>(ctxt);
    self->main();

    // Switch back to the resume point
#if defined(_WIN32)
    ::SwitchToFiber(self->win32.caller);
    ILIAS_ASSERT(false, "Should not reach here");
#if defined(__cpp_lib_unreachable)
    std::unreachable();
#endif // __cpp_lib_unreachable
#endif // _WIN32

}

#if !defined(_WIN32)
auto ucontextEntry() -> void {
    callContext(currentContext); // We use uc_link to return to the caller
}
#endif // _WIN32

} // namespace

// User interface
// MARK: User
auto FiberContext::destroy() -> void {
    auto self = static_cast<FiberContextImpl *>(this);
    self->destroyImpl();
}

auto FiberContext::resume() -> bool {
    auto self = static_cast<FiberContextImpl *>(this);
    return self->resumeImpl();
}

auto FiberContext::wait(runtime::CaptureSource where) -> void {
    auto self = static_cast<FiberContextImpl *>(this);
    if (!resume()) { // If not complete, enter the event loop
        auto stopSource = runtime::StopSource {};
        auto handler = [](auto ctxt, void *source) {
            static_cast<runtime::StopSource *>(source)->request_stop();
        };
        self->mCompletionHandler = handler;
        self->mUser = &stopSource;
        self->mExecutor->run(stopSource.get_token());
    }
    ILIAS_ASSERT(self->mComplete);
}

auto FiberContext::setExecutor(runtime::Executor &e) -> void {
    auto self = static_cast<FiberContextImpl *>(this);
    self->mExecutor = &e;
}

auto FiberContext::create4(FiberEntry entry, runtime::CaptureSource source) -> FiberContext * {
    auto ctxt = std::make_unique<FiberContextImpl>();

    ILIAS_ASSERT(entry.invoke);
    ctxt->mCreation = source;
    ctxt->mEntry.args = entry.args;
    ctxt->mEntry.invoke = entry.invoke;
    ctxt->mEntry.cleanup = entry.cleanup;

#if defined(_WIN32)
    ctxt->win32.handle = ::CreateFiberEx(
        entry.stackSize, 
        0, 
        FIBER_FLAG_FLOAT_SWITCH, 
        callContext, 
        ctxt.get()
    );
#else
    if (entry.stackSize == 0) {
        entry.stackSize = 1024 * 1024; // Use 1MB
    }

    // Allocate a stack
    size_t pageSize = ::sysconf(_SC_PAGESIZE);
    size_t mmapSize = entry.stackSize + pageSize; // Add one guard page
    auto mmapPtr = ::mmap(nullptr, mmapSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (mmapPtr == MAP_FAILED) {
        ILIAS_THROW(std::bad_alloc{});
    }
    if (::mprotect(mmapPtr, pageSize, PROT_NONE) != 0) {
        ::munmap(mmapPtr, mmapSize);
        ILIAS_THROW(std::bad_alloc{});
    }

    // Calc the stack
    auto stack = static_cast<char *>(mmapPtr) + pageSize;
    ctxt->posix.mmapPtr = mmapPtr;
    ctxt->posix.mmapSize = mmapSize;

    // Create the context
    sys::getcontext(&ctxt->posix.self);
    ctxt->posix.self.uc_stack.ss_sp = stack;
    ctxt->posix.self.uc_stack.ss_size = entry.stackSize;
    ctxt->posix.self.uc_link = &ctxt->posix.caller; // Return to the caller
    sys::makecontext(&ctxt->posix.self, ucontextEntry, 0);
#endif // _WIN32

    return ctxt.release();
}

auto FiberContext::valuePointer() -> void * {
    auto self = static_cast<FiberContextImpl *>(this);
    ILIAS_ASSERT(self->mComplete, "Fiber not complete yet");
    ILIAS_ASSERT(!self->mStopped, "Fiber is stopped, no value provided");
    if (self->mException) {
        std::rethrow_exception(self->mException);
    }
    return self->mValue;
}

} // namespace fiber

using namespace fiber;

auto this_fiber::yield() -> void {
    auto cur = FiberContextImpl::current();
    cur->schedule();
    cur->suspend();
}

auto this_fiber::stopToken() -> runtime::StopToken {
    return FiberContextImpl::current()->mStopSource.get_token();
}

auto this_fiber::await4(runtime::CoroHandle coro, runtime::CaptureSource source) -> void {
    auto fiber = FiberContextImpl::current();
    auto ctxt = runtime::CoroContext {};
    auto handler = [](runtime::CoroContext &ctxt) {
        auto self = static_cast<FiberContextImpl *>(ctxt.userdata());
        if (self) {
            self->schedule();
        }
    };

    // Forward the stop 
    auto cb = runtime::StopCallback(fiber->mStopSource.get_token(), [&]() {
        ctxt.stop();
    });

    // Begin execute
    ctxt.setExecutor(*fiber->mExecutor);
    ctxt.setStoppedHandler(handler);
    coro.setCompletionHandler(handler);
    coro.setContext(ctxt);
    coro.resume();
    if (!coro.done()) {
        ctxt.setUserdata(fiber);
        fiber->suspend(); // Suspend self, wait for the coro to complete
    }
    ILIAS_ASSERT(coro.done() || ctxt.isStopped());
    // Ok check
    if (ctxt.isStopped()) {
        throw FiberCancellation {};
    }
}

// MARK: FiberAwaiter
auto FiberAwaiterBase::await_suspend(runtime::CoroHandle caller) -> void {
    auto handle = static_cast<FiberContextImpl *>(mHandle.get());
    handle->mCompletionHandler = onCompletion;
    handle->mUser = this;
    mCaller = caller;
    mReg.register_<&FiberAwaiterBase::onStopRequested>(caller.stopToken(), this);
}

auto FiberAwaiterBase::onStopRequested() -> void {
    static_cast<FiberContextImpl*>(mHandle.get())->mStopSource.request_stop(); // forward the stop request to the fiber
}

auto FiberAwaiterBase::onCompletion(FiberContext *ctxt, void *_self) -> void {
    auto self = static_cast<FiberAwaiterBase *>(_self);
    if (static_cast<FiberContextImpl*>(ctxt)->mStopped) {
        self->mCaller.setStopped(); // Forward the stop to the caller
        return;
    }
    self->mCaller.schedule();
}

ILIAS_NS_END

#endif // ILIAS_USE_FIBER