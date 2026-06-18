#include <ilias/runtime/exception.hpp> // ExceptionPtr
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/fiber.hpp> // Fiber
#include <cstdint>
#include <limits>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp> // CreateFiberEx
#elif __has_include(<ucontext.h>)
    #include "libucontext.hpp" // sys::getcontext, sys::makecontext
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

    // Environment Guard
    [[ILIAS_NO_UNIQUE_ADDRESS]]
    FiberInitializer mInitializer;

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

    // Internal state
    bool mRunning = false;
    bool mStarted = false;

    // Handler invoked when complete
    void (*mCompletionHandler)(FiberContext *ctxt, void *) = nullptr;
    void  *mUser = nullptr;

    // Entry
    FiberEntry *mEntry = nullptr;

    // Result
    void *mValue = nullptr;
    runtime::ExceptionPtr mException;

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

#if defined(_WIN32) // Not in windows, we should save it by ourselves
static constinit thread_local size_t gInitializeCount {0}; // MAX on current thread is already a fiber
#else
static constinit thread_local FiberContextImpl *gCurrentContext {};

struct CurrentGuard { // RAII guard for manage the current fiber
    CurrentGuard(FiberContextImpl *c) : cur(c) {
        prev = std::exchange(gCurrentContext, cur);
    }
    ~CurrentGuard() {
        gCurrentContext = prev;
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
        mValue = mEntry->invoke(mEntry);
    }
    catch (FiberCancellation &) {
        mStopped = true;
    }
    catch (...) { // Another user exception
        mException = runtime::ExceptionPtr::currentException();
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

    if (mEntry) {
        mEntry->destroy(mEntry);
    }
    delete this;
}

auto FiberContextImpl::resumeImpl() -> bool {
    ILIAS_ASSERT(!mRunning && !mComplete, "Cannot resume a running or complete fiber");

#if defined(_WIN32)
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
    mExecutor->schedule([this]() {
        resumeImpl();
    });
}

auto FiberContextImpl::current() -> FiberContextImpl * {
    FiberContextImpl *ptr = nullptr;
#if defined(_WIN32)
    // boost/context/continuation_winfib.hpp
    // 0x1E00 is a magic number that current thread is not a fiber
    if (auto fiber = ::GetCurrentFiber(); fiber != nullptr || fiber != reinterpret_cast<void*>(0x1E00)) [[likely]] {
        auto data = static_cast<FiberContextImpl *>(::GetFiberData());
        ILIAS_ASSERT(data->mMagic == 0x114514, "Magic number mismatch, memory corrupted ???");
        ptr = data;
    }
#else
    ptr = gCurrentContext;
#endif // _WIN32
    if (!ptr) [[unlikely]] {
        ILIAS_THROW(std::runtime_error{"No current fiber"});
    }
    return ptr;
}

auto callContext(FiberContextImpl *self) -> void {
    self->main();

    // Switch back to the resume point
#if defined(_WIN32)
    ::SwitchToFiber(self->win32.caller);
    ILIAS_ASSERT(false, "Should not reach here");
    ILIAS_UNREACHABLE();
#endif // _WIN32

}

#if !defined(_WIN32)
auto ucontextEntry() -> void {
    callContext(gCurrentContext); // We use uc_link to return to the caller
}
#endif // _WIN32

} // namespace

// User interface
// MARK: User
auto FiberContext::destroy() -> void {
    return static_cast<FiberContextImpl *>(this)->destroyImpl();
}

auto FiberContext::resume() -> bool {
    return static_cast<FiberContextImpl *>(this)->resumeImpl();
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

auto FiberContext::create4(FiberEntry *entry) -> FiberContext * {
    auto ctxt = std::make_unique<FiberContextImpl>();

    ctxt->mEntry = entry;

#if defined(_WIN32)
    ctxt->win32.handle = ::CreateFiberEx(
        entry->stackSize, 
        0, 
        FIBER_FLAG_FLOAT_SWITCH, 
        [](void *ctxt) {
            return callContext(static_cast<FiberContextImpl *>(ctxt));
        },
        ctxt.get()
    );
#else
    if (entry->stackSize == 0) {
        entry->stackSize = 1024 * 1024; // Use 1MB
    }

    // Allocate a stack
    size_t pageSize = ::sysconf(_SC_PAGESIZE);
    size_t mmapSize = entry->stackSize + pageSize; // Add one guard page
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
    ctxt->posix.self.uc_stack.ss_size = entry->stackSize;
    ctxt->posix.self.uc_link = &ctxt->posix.caller; // Return to the caller
    sys::makecontext(&ctxt->posix.self, ucontextEntry, 0);
#endif // _WIN32

    return ctxt.release();
}

auto FiberContext::valuePointer() -> void * {
    auto self = static_cast<FiberContextImpl *>(this);
    ILIAS_ASSERT(self->mComplete, "Fiber not complete yet");
    ILIAS_ASSERT(!self->mStopped, "Fiber is stopped, no value provided");
    self->mException.rethrowIfAny();
    return self->mValue;
}

// Handle the environment
#if defined(_WIN32)
auto initialize() -> void {
    if (gInitializeCount > 0) { // Information probed
        if (gInitializeCount != std::numeric_limits<size_t>::max()) {
            gInitializeCount++;
        }
        return;
    }
    if (::IsThreadAFiber()) {
        gInitializeCount = std::numeric_limits<size_t>::max();
        return;
    }
    if (!::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH)) {
        ILIAS_THROW(std::runtime_error{"Failed to ConvertThreadToFiberEx"});
    }
    gInitializeCount = 1;
}

auto shutdown() -> void {
    ILIAS_ASSERT(gInitializeCount > 0, "Fiber not initialized, but shutdown called");
    if (gInitializeCount == std::numeric_limits<size_t>::max()) {
        return;
    }
    if (--gInitializeCount == 0) {
        if (!::ConvertFiberToThread()) { // Why?, didwe need to handle it or just abort?
            ILIAS_ERROR("Fiber", "Failed to ConvertFiberToThread {}", ::GetLastError());
        }
    }
}
#else // No-op
auto initialize() -> void {}
auto shutdown() -> void {}
#endif // _WIN32

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

auto this_fiber::awaitImpl(runtime::CoroHandle handle, runtime::CaptureSource source) -> void {
    auto handler = [](runtime::CoroContext &ctxt) {
        auto self = static_cast<FiberContextImpl *>(ctxt.userdata());
        if (self) {
            self->schedule();
        }
    };
    auto fiber = FiberContextImpl::current();

    // Forward the stop to the context
    runtime::CoroContext ctxt {};
    runtime::StopCallback callback {fiber->mStopSource.get_token(), [&]() {
        ctxt.stop();
    }};

    // Begin execute
    ctxt.setExecutor(*fiber->mExecutor);
    ctxt.setStoppedHandler(handler);
    handle.setCompletionHandler(handler);
    handle.setContext(ctxt);

    // Execute it
    handle.resume();
    if (!handle.done()) {
        ctxt.setUserdata(fiber);

        // Suspend self, wait for the coro to complete
        fiber->mSuspendPoint = source;
        fiber->suspend();
    }
    ILIAS_ASSERT(handle.done() || ctxt.isStopped(), "The coroutine should be stopped or done");

    // Ok check the stop, if it is stopped, forward the stop to the caller
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