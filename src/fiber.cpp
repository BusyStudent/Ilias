#include <ilias/defines.hpp>

// #if defined(ILIAS_USE_FIBER)
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/runtime/token.hpp> // StopToken
#include <ilias/fiber/fiber.hpp> // Fiber

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp> // CreateFiber
#endif // _WIN32

#if defined(__linux)
    #include <ucontext.h> // getcontext, makecontext
#endif // __linux

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
        HANDLE handle = nullptr;
        HANDLE caller = nullptr;
    } win32;
#endif // _WIN32

    // Method
    auto main() -> void;

    // Impl of the user interface
    auto resumeImpl() -> void;
    auto suspendImpl() -> void;
    auto destroyImpl() -> void;
};

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
        mUnwinding = true;
        resumeImpl();
        ILIAS_ASSERT(mComplete);
    }
    // Ok, safe to destroy
#if defined(_WIN32)
    ::DeleteFiber(win32.handle);
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
#endif // _WIN32

}

auto FiberContextImpl::suspendImpl() -> void {
    ILIAS_ASSERT_MSG(running && !mComplete, "Cannot suspend a non-running or complete fiber");
    running = false;

#if defined(_WIN32)
    auto caller = std::exchange(win32.caller, nullptr);
    ::SwitchToFiber(caller); // Switch back to the resume point
#endif // _WIN32

    running = true;
    if (mUnwinding) {
        throw FiberUnwind();
    }
}

auto callContext(void *ctxt) -> void {
    auto self = static_cast<FiberContextImpl *>(ctxt);
    self->main();

    // Switch back to the resume point
#if defined(_WIN32)
    ::SwitchToFiber(self->win32.caller);
#endif // _WIN32

    ILIAS_ASSERT_MSG(false, "Should not reach here");
}

} // namespace

// User interface
#pragma region User
auto FiberContext::current() -> FiberContext * {

#if defined(_WIN32)
    auto data = static_cast<FiberContextImpl *>(::GetFiberData());
    ILIAS_ASSERT(data->magic == 0x114514);
    return data;
#else
    ::abort();
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

auto FiberContext::create4(FiberEntry entry) -> FiberContext *{
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
    struct CoroContext : runtime::CoroContext {
        FiberContextImpl *fiber = nullptr;
    };
    CoroContext ctxt;
    auto handler = [](runtime::CoroContext &_self) {
        auto &self = static_cast<CoroContext &>(_self);
        if (self.fiber) {
            self.fiber->schedule();
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
        ctxt.fiber = fiber;
        fiber->suspend(); // Suspend self, wait for the coro to complete
    }
    ILIAS_ASSERT(coro.done() || ctxt.isStopped());
    // Ok check
    if (ctxt.isStopped()) {
        throw FiberUnwind();
    }
}

ILIAS_NS_END

// #endif