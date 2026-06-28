#include <ilias/platform/detail/blocking.hpp>
#include <ilias/io/system_error.hpp> // SystemError
#include <ilias/log.hpp>
#include <unistd.h>

ILIAS_NS_BEGIN

auto runtime::threadpool::read(fd_t fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    if (co_await this_coro::isStopRequested()) {
        co_await this_coro::stopped();
    }
    co_return co_await blocking([&]() -> IoResult<size_t> {
        ::ssize_t bytes = 0;
        if (offset) {
            bytes = ::pread(fd, buffer.data(), buffer.size(), *offset);
        }
        else {
            bytes = ::read(fd, buffer.data(), buffer.size());
        }
        if (bytes < 0) {
            return Err(SystemError::fromErrno());
        }
        return bytes;
    });
}

auto runtime::threadpool::write(fd_t fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    if (co_await this_coro::isStopRequested()) {
        co_await this_coro::stopped();
    }
    co_return co_await blocking([&]() -> IoResult<size_t> {
        ::ssize_t bytes = 0;
        if (offset) {
            bytes = ::pwrite(fd, buffer.data(), buffer.size(), *offset);
        }
        else {
            bytes = ::write(fd, buffer.data(), buffer.size());
        }
        if (bytes < 0) {
            return Err(SystemError::fromErrno());
        }
        return bytes;
    });
}

ILIAS_NS_END