#include <ilias/defines.hpp>

#if defined(ILIAS_USE_FIBER)
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/fiber/fiber.hpp> // Fiber
#include "singleton.hpp" // Singleton

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp> // CreateFiber
#elif __has_include(<ucontext.h>)
    #include <ucontext.h> // getcontext, makecontext
#else
    #error "No fiber support on this platform"
#endif // _WIN32

ILIAS_NS_BEGIN

using namespace fiber;

namespace {

#pragma region Impl
// The hidden implment of the fiber
class FiberContextImpl final : public FiberContext {
public:
    // Guard magic
    uint32_t magic = 0x114514;

    // Internal state
    bool running = false;
    bool started = false;

#if defined(_WIN32)
    // Platform data
    struct win32 {
        ::HANDLE handle = nullptr;
        ::HANDLE caller = nullptr;
    } win32;
#else
    struct {
        ::ucontext_t caller {};
        ::ucontext_t self {};
    } posix;
#endif // _WIN32

    // Method
    auto main() -> void;

    // Impl of the user interface
    auto resumeImpl() -> void;
    auto suspendImpl() -> void;
    auto destroyImpl() -> void;
};

#if !defined(_WIN32) // Not in windows, we should save it by ourselves
static constinit thread_local Singleton<FiberContext*> currentContext {};

struct CurrentGuard { // RAII guard for manage the current fiber
    CurrentGuard(FiberContextImpl *c) : cur(c) {
        prev = static_cast<FiberContextImpl*>(
            static_cast<FiberContext*>(currentContext) // Because the Singleton may be a proxy
        );
        currentContext = cur;
    }
    ~CurrentGuard() {
        currentContext = prev;
    }
    FiberContextImpl *cur = nullptr;
    FiberContextImpl *prev = nullptr;
};
#endif // _WIN32

auto FiberContextImpl::main() -> void {
    started = true;
    running = true;

    // Call the entry
    try {
        mValue = mEntry.invoke(mEntry.args);
    }
    catch (FiberUnwind &e) {
        mStopped = true;
    }
    catch (...) { // Another user exception
        mException = std::current_exception();
    }
    mComplete = true;
    running = false;

    // Notify
    if (mCompletionHandler) {
        mCompletionHandler(this, mUser);
    }
}

auto FiberContextImpl::destroyImpl() -> void {
    ILIAS_ASSERT_MSG(!running, "Cannot destroy a running fiber");
    if (started && !mComplete) { // Started, but suspend
        ILIAS_ASSERT_MSG(false, "Cannot destroy a suspended fiber");
    }
    // Ok, safe to destroy
#if defined(_WIN32)
    ::DeleteFiber(win32.handle);
#else
    ::free(posix.self.uc_stack.ss_sp);
#endif // _WIN32

    // Destroy the entry
    if (mEntry.cleanup) {
        mEntry.cleanup(mEntry.args);
    }
    delete this;
}

auto FiberContextImpl::resumeImpl() -> void {
    ILIAS_ASSERT_MSG(!running && !mComplete, "Cannot resume a running or complete fiber");

#if defined(_WIN32)
    struct ConvertGuard {
        ConvertGuard() { // The main fiber may use float point, save it
            auto ret = ::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
            ILIAS_ASSERT(ret);
        }
        ~ConvertGuard() {
            auto ok = ::ConvertFiberToThread();
            ILIAS_ASSERT(ok);
        }
    };
    static thread_local ConvertGuard guard;
    win32.caller = ::GetCurrentFiber();
    ::SwitchToFiber(win32.handle);
#else
    CurrentGuard guard(this);
    ::swapcontext(&posix.caller, &posix.self);
#endif // _WIN32

}

auto FiberContextImpl::suspendImpl() -> void {
    ILIAS_ASSERT_MSG(running && !mComplete, "Cannot suspend a non-running or complete fiber");
    running = false;

    // Switch back to the resume point
#if defined(_WIN32)
    auto caller = std::exchange(win32.caller, nullptr);
    ::SwitchToFiber(caller);
#else
    ::swapcontext(&posix.self, &posix.caller);
#endif // _WIN32

    running = true;
}

auto callContext(void *ctxt) -> void {
    auto self = static_cast<FiberContextImpl *>(ctxt);
    self->main();

    // Switch back to the resume point
#if defined(_WIN32)
    ::SwitchToFiber(self->win32.caller);
    ILIAS_ASSERT_MSG(false, "Should not reach here");
#endif // _WIN32

}

#if !defined(_WIN32)
auto ucontextEntry() -> void {
    callContext(currentContext); // We use uc_link to return to the caller
}
#endif // _WIN32

} // namespace

// User interface
#pragma region User
auto FiberContext::current() -> FiberContext * {

#if defined(_WIN32)
    auto data = static_cast<FiberContextImpl *>(::GetFiberData());
    ILIAS_ASSERT(data->magic == 0x114514);
    return data;
#else
    return currentContext;
#endif // _WIN32

}

auto FiberContext::suspend() -> void {
    auto self = static_cast<FiberContextImpl *>(current());
    ILIAS_ASSERT_MSG(self, "No fiber context");
    self->suspendImpl();
}

auto FiberContext::destroy() -> void {
    auto self = static_cast<FiberContextImpl *>(this);
    self->destroyImpl();
}

auto FiberContext::resume() -> void {
    auto self = static_cast<FiberContextImpl *>(this);
    self->resumeImpl();
}

auto FiberContext::schedule() -> void {
    auto self = static_cast<FiberContextImpl *>(this);
    mExecutor->post([](void *ptr) {
        static_cast<FiberContextImpl *>(ptr)->resumeImpl();
    }, self);
}

auto FiberContext::create4(FiberEntry entry) -> FiberContext * {
    auto ctxt = std::make_unique<FiberContextImpl>();

    ILIAS_ASSERT(entry.invoke);
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
        entry.stackSize = 1024 * 32; // Use 32K
    }
    ::getcontext(&ctxt->posix.self);
    ctxt->posix.self.uc_stack.ss_sp = ::malloc(entry.stackSize);
    ctxt->posix.self.uc_stack.ss_size = entry.stackSize;
    ctxt->posix.self.uc_link = &ctxt->posix.caller; // Return to the caller
    ::makecontext(&ctxt->posix.self, ucontextEntry, 0);
#endif // _WIN32

    return ctxt.release();
}

auto this_fiber::yield() -> void {
    auto cur = FiberContext::current();
    cur->schedule();
    cur->suspend();
}

auto this_fiber::await4(runtime::CoroHandle coro) -> void {
    auto fiber = static_cast<FiberContextImpl *>(FiberContext::current());
    runtime::CoroContext ctxt;
    auto handler = [](runtime::CoroContext &ctxt) {
        auto self = static_cast<FiberContext*>(ctxt.userdata());
        if (self) {
            self->schedule();
        }
    };

    // Forward the stop 
    auto cb = runtime::StopCallback(fiber->stopToken(), [&]() {
        ctxt.stop();
    });

    // Begin execute
    ctxt.setExecutor(fiber->executor());
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
        throw FiberUnwind();
    }
}

ILIAS_NS_END

#endif // ILIAS_USE_FIBER