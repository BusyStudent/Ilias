#pragma once

#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>

ILIAS_NS_BEGIN

/**
 * @brief Poller class, use to poll events on fd / sockets, it doesn't own the fd, just borrows it
 * 
 */
class Poller {
public:
    Poller() = default;
    Poller(IoHandle<fd_t> h) : mHandle(std::move(h)) { }

    /**
     * @brief Close the current poller
     * 
     */
    auto close() {
        return mHandle.close();
    }

    auto cancel() {
        return mHandle.cancel();
    }

    /**
     * @brief Poll for events on the fd
     * 
     * @param events The events to poll for (like PollEvent::In | PollEvent::Out)
     * @return IoTask<uint32_t> The reveived events
     */
    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mHandle.poll(events);
    }
    /**
     * @brief Get the fd of the poller
     * 
     * @return fd_t 
     */
    auto fd() const -> fd_t {
        return mHandle.fd();
    }

    /**
     * @brief Create a new poller by borrowing a fd
     * 
     * @param fd    The fd to borrow
     * @param type  The type of the fd (default is IoDescriptor::Unknown)
     * @return IoTask<Poller> 
     */
    static auto make(fd_t fd, IoDescriptor::Type type = IoDescriptor::Unknown) -> IoTask<Poller> {
        auto handle = IoHandle<fd_t>::make(fd, type);
        if (!handle) {
            co_return Err(handle.error());
        }
        co_return Poller(std::move(*handle));
    }

#if defined(_WIN32)
    static auto make(socket_t sockfd) -> IoTask<Poller> {
        return make(fd_t(sockfd), IoDescriptor::Socket);
    }
#endif

    /**
     * @brief Check the poller is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    IoHandle<fd_t> mHandle;
};

ILIAS_NS_END