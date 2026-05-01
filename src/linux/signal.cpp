#include <ilias/runtime/token.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp> // IoContext, IoHandle
#include <ilias/io/fd.hpp> // FileDescriptor
#include <ilias/signal.hpp>
#include <unistd.h>
#include <csignal>
#include <atomic>
#include <mutex> // std::call_once

#if !defined(NSIG)
    #error "Require NSIG macro for signal handling"
#endif

ILIAS_NS_BEGIN

namespace signal {

// https://www.man7.org/linux/man-pages/man2/sigprocmask.2.html
// man sigprocmask: The use of sigprocmask() is unspecified in a multithreaded process; see pthread_sigmask(3).
// and the signalfd require to block the signal in all threads.
// As a library we can't assume that, so use the self-pipe trick and signal handler.
namespace {
    // 0 fd in most unix platform is STDIN_ERRNO, treat as invalid
    static_assert(STDIN_FILENO == 0 || STDERR_FILENO == 0 || STDOUT_FILENO == 0, "Sentinel value for fd should be 0");
    static_assert(std::atomic<int>::is_always_lock_free, "std::atomic<int> should be lock free for signal handling");

    // The environment of the signal handler
    struct SignalSlot {
        union {
            void (*action)(int sig, siginfo_t *info, void *ucontext);
            void (*handler)(int sig) = nullptr;
        };
        std::once_flag   once   {};
        std::atomic<int> writer {};
    };

    static constinit bool       signalActions[NSIG] {}; // true on is action
    static constinit SignalSlot signalSlots[NSIG] {};
    static auto actionHandler(int sig, ::siginfo_t *info, void *ctxt) -> void {
        auto &slot = signalSlots[sig];

        // Write the signal to the pipe
        if (auto writer = slot.writer.exchange(0); writer > 0) {
            char byte = 0;
            ::write(writer, &byte, sizeof(byte));
            ::close(writer);
        }
        // Chain the handler if needed
        if (slot.handler != SIG_DFL && slot.handler != SIG_IGN && slot.handler) {
            if (signalActions[sig]) {
                slot.action(sig, info, ctxt);
            }
            else {
                slot.handler(sig);
            }
        }
    }
}

auto waitFor(int sig) -> IoTask<void> {
    if (sig < 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
        co_return Err(SystemError::InvalidArgument);
    }

    // Prepare the signal handler
    auto &slot = signalSlots[sig];
    std::call_once(slot.once, [&] {
        struct sigaction action {};
        struct sigaction prev {};
        action.sa_sigaction = actionHandler;
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        ::sigemptyset(&action.sa_mask);
        if (::sigaction(sig, &action, &prev) == -1) {
            ILIAS_THROW(std::system_error(std::error_code(errno, std::system_category()), "sigaction"));
        }

        // Chain it
        if (prev.sa_flags & SA_SIGINFO) {
            signalActions[sig] = true;
            slot.action = prev.sa_sigaction;
        }
        else {
            slot.handler = prev.sa_handler;
        }
    });

    struct Guard {
        ~Guard() {
            int expected = pipes[1];
            if (signalSlots[sig].writer.compare_exchange_strong(expected, 0)) { // This slot store our fd
                ::close(pipes[1]); // FIXME: Due the fd reuse, race condition can happen
            }
            if (pipes[0] != -1) {
                ::close(pipes[0]);
            }
        }

        int sig {};
        int pipes[2] {-1, -1};
    } guard {sig};

    // Prepare the pipe
    if (::pipe2(guard.pipes, O_NONBLOCK | O_CLOEXEC) == -1) {
        co_return Err(SystemError::fromErrno());
    }

    // Move the ownship of write fd to the shared state
    int expected = 0; // 0 on this slot is empty
    if (!slot.writer.compare_exchange_strong(expected, guard.pipes[1])) {
        co_return Err(SystemError(EBUSY));
    }

    // Wait for the signal
    auto handle = IoHandle<fd_t>::make(guard.pipes[0], IoDescriptor::Pipe);
    if (!handle) {
        co_return Err(handle.error());
    }
    char mark = 0;
    if (auto res = co_await handle->read(makeBuffer(&mark, sizeof(mark)), std::nullopt); !res) {
        co_return Err(res.error());
    }
    co_return {};
}

auto ctrlC() -> IoTask<void> {
    return waitFor(SIGINT);   
}

} // namespace signal

ILIAS_NS_END