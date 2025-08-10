#include <ilias/detail/win32defs.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp>
#include <ilias/signal.hpp>
#include <csignal>

ILIAS_NS_BEGIN

namespace {
    static constinit std::atomic<runtime::CoroHandle> waiter;

    auto WINAPI ctrlCHandler(DWORD type) -> BOOL {
        auto handle = waiter.exchange(nullptr);
        if (handle) {
            handle.schedule();
        }
        if (!::SetConsoleCtrlHandler(ctrlCHandler, FALSE)) {
            ILIAS_ERROR("Signal", "Failed to disable ctrlC handler {}", SystemError::fromErrno());
        }
        return TRUE;
    }
}

auto signal::ctrlC() -> IoTask<void> {
    if (waiter.load()) {
        co_return Err(IoError::InProgress);
    }
    struct Awaiter {
        auto await_ready() -> bool { return false; }
        auto await_suspend(runtime::CoroHandle handle) -> bool {
            waiter = handle;
            if (!::SetConsoleCtrlHandler(ctrlCHandler, true)) {
                waiter = nullptr;
                err = SystemError::fromErrno();
                return false;
            }
            reg.register_<&Awaiter::onStopRequested>(handle.stopToken(), this);
            return true;
        }
        auto await_resume() -> IoResult<void> {
            if (!err.isOk()) {
                return Err(err);
            }
            return {};
        }
        auto onStopRequested() -> void {
            auto handle = waiter.exchange(nullptr);
            if (!::SetConsoleCtrlHandler(ctrlCHandler, FALSE)) {
                ILIAS_ERROR("Signal", "Failed to disable ctrlC handler {}", SystemError::fromErrno());
            }
            handle.setStopped();
        }

        runtime::StopRegistration reg;
        SystemError err;
    };
    co_return co_await Awaiter{};
}

ILIAS_NS_END