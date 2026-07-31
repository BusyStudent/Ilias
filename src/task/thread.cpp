#include <ilias/task/thread.hpp> // Thread
#include <ilias/task/spawn.hpp> // spawn
#include <ilias/platform.hpp> // PlatformContext
#include <utility> // std::exchange
#include <atomic> // std::atomic_ref

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#endif // _WIN32

ILIAS_NS_BEGIN

using namespace task;

// MARK: Thread
auto ThreadBase::start() -> void {
    if (!mInit) {
        mInit = []() -> Executor * {
            return new PlatformContext;
        };
    }
    mThread = std::thread([this]() {
        // Make the executor
        auto executor = std::unique_ptr<Executor> { mInit() };
        executor->install();

        // Start
        ILIAS_TRY_EXCEPTION {
            auto taskHandle = spawn(mInvoke(*this));
            auto cb = runtime::StopCallback(mSource.get_token(), [&, stopHandle = taskHandle.stopHandle()]() {
                // Forward the stop to the task
                executor->schedule([&]() {
                    stopHandle.stop();
                }); // We need call it on the current thread
            });

            // Wait done
            taskHandle.wait();
        }
        ILIAS_CATCH (...) {
            mException = ExceptionPtr::currentException();
        }

        // Ok try wakeup the awaiter
        auto caller = [&]() {
            std::lock_guard locker {mMutex};
            mCompleted = true;
            return mHandle;
        }();

        if (!caller) {
            return;
        }
        caller.executor().schedule([caller, this]() { // Back to the caller thread
            if (mSource.stop_requested() && caller.isStopRequested()) {
                caller.setStopped();
                return;
            }
            caller.resume();
        });
    });
}

auto ThreadBase::setName(std::string_view name) -> void {

#if defined(_WIN32)
    using T = std::thread::native_handle_type;
    auto winHandle = [](auto handle) -> HANDLE {
        if constexpr (std::is_same_v<T, HANDLE>) { // Normal msvc
            return handle;
        }
#if defined(__MINGW32__) // Handle the mingw
        if constexpr (std::is_same_v<T, pthread_t>) {
            return ::pthread_gethandle(handle);
        }
#endif // __MINGW32__
        else {
            static_assert(std::is_same_v<decltype(handle), void>, "Unsupport thread handle type");
        }
    }(mThread.native_handle());

    win32::setThreadName(winHandle, name);
#else
    ::pthread_setname_np(mThread.native_handle(), std::string {name}.c_str());
#endif // _WIN32

}

ILIAS_NS_END