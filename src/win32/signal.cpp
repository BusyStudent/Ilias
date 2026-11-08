#include <ilias/detail/scope_exit.hpp> // ScopeExit
#include <ilias/detail/win32defs.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp> // win32::waitObject
#include <ilias/io/fd.hpp> // Win32Handle
#include <ilias/signal.hpp>
#include <algorithm> // std::find
#include <mutex> // std::call_once

ILIAS_NS_BEGIN

namespace signal {
namespace {
    // Win32 ABI is stable. used the maximum value from the header
    static_assert(CTRL_SHUTDOWN_EVENT == 6, "ABI mismatch"); // The last event
    constexpr std::array CTRL_EVENTS_SET = {CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT, CTRL_SHUTDOWN_EVENT};
    constexpr int CTRL_EVENT_MAX = CTRL_SHUTDOWN_EVENT + 1;

    struct ConsoleCtrlHandlers {
        Win32Handle         slots[CTRL_EVENT_MAX]   {}; // nullptr on unregister, INVALID_HANDLE_VALUE on updating
        std::atomic<size_t> waiters[CTRL_EVENT_MAX] {};

        ConsoleCtrlHandlers() {
            // We create all event in the set at once, simplifying the logic...
            for (auto i : CTRL_EVENTS_SET) {
                slots[i] = Win32Handle{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
                if (!slots[i]) [[unlikely]] {
                    ILIAS_THROW(std::system_error{SystemError::fromErrno()});
                }
            }

            // Register the handler
            if (!::SetConsoleCtrlHandler(onNotify, TRUE)) [[unlikely]] {
                ILIAS_THROW(std::system_error{SystemError::fromErrno()});
            }
        }
        ~ConsoleCtrlHandlers() {
            ::SetConsoleCtrlHandler(onNotify, FALSE);
        }

        static auto instance() -> ConsoleCtrlHandlers & {
            static ConsoleCtrlHandlers c;
            return c;
        }

        // Notify
        static auto WINAPI onNotify(DWORD type) -> BOOL {
            auto &c = instance();
            if (type >= CTRL_EVENT_MAX) [[unlikely]]{ // Skil the unknown events
                return FALSE;
            }

            // TODO: Handle the CLOSE, LOGOFF, SHUTDOWN events specialy
            if (c.slots[type] && c.waiters[type].load() > 0) { // Notify if registered?
                ::SetEvent(c.slots[type].get());
                return TRUE;
            }
            return FALSE; // Not registered, keep default behavior
        }
    };
} // namespace

auto ctrlC() -> IoTask<void> {
    return waitCtrl(CTRL_C_EVENT);
}

auto waitCtrl(int ctrlEvent) -> IoTask<void> {
    // TODO: Race condition? wakeup may lost, using a watch channel instead?
    if (std::ranges::find(CTRL_EVENTS_SET, ctrlEvent) == CTRL_EVENTS_SET.end()) {
        co_return Err(IoError::InvalidArgument);
    }
    auto &c = ConsoleCtrlHandlers::instance();
    c.waiters[ctrlEvent].fetch_add(1);
    ScopeExit guard{[&]() {
        if (c.waiters[ctrlEvent].fetch_sub(1) == 1) { // Is the last waiter?
            ::ResetEvent(c.slots[ctrlEvent].get()); // Reset the event
        }
    }};
    co_return co_await c.slots[ctrlEvent].wait(); // Waiting for the events to be set
}

} // namespace signal

ILIAS_NS_END