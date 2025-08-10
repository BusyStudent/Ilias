#include <ilias/runtime/token.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/context.hpp> // IoContext, IoHandle
#include <ilias/io/fd.hpp> // FileDescriptor
#include <ilias/signal.hpp>
#include <sys/signalfd.h> // signalfd
#include <unistd.h>
#include <csignal>

ILIAS_NS_BEGIN

namespace signal {

auto signal(int sig) -> IoTask<void> {
    ::sigset_t set {};
    ::sigemptyset(&set);
    ::sigaddset(&set, sig);
    if (::sigprocmask(SIG_BLOCK, &set, nullptr) == -1) {
        co_return Err(SystemError::fromErrno());
    }
    struct Guard {
        ~Guard() {
            ::sigprocmask(SIG_UNBLOCK, &set, nullptr);
        }
        ::sigset_t &set;
    } guard {set};

    auto fd = FileDescriptor(::signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC));
    if (fd.get() == -1) {
        co_return Err(SystemError::fromErrno());
    }
    auto handle = IoHandle<FileDescriptor>::make(std::move(fd), IoDescriptor::Pollable);
    if (!handle) {
        co_return Err(handle.error());
    }
    // Wait for the signal
    ::signalfd_siginfo info;
    if (auto res = co_await handle->read(makeBuffer(&info, sizeof(info)), std::nullopt); !res) {
        co_return Err(res.error());
    }
    co_return {};
}

auto ctrlC() -> IoTask<void> {
    return signal(SIGINT);   
}

} // namespace signal

ILIAS_NS_END