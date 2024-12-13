#pragma once

#include <ilias/io/context.hpp>
#include <ilias/net/system.hpp>

ILIAS_NS_BEGIN

/**
 * @brief Poller class, use to poll events on fd / sockets
 * 
 */
class Poller {
public:
    Poller() = default;

    /**
     * @brief Construct a new Poller object
     * 
     * @param ctxt The IoContext to use
     * @param fd The fd to poll
     * @param type The type of descriptor (default: Unknown, let the backend guess)
     */
    Poller(IoContext &ctxt, fd_t fd, IoDescriptor::Type type = IoDescriptor::Unknown) {
        mCtxt = &ctxt;
        mDesc = mCtxt->addDescriptor(fd, type).value_or(nullptr);
        mFd = fd;
    }

#if defined(_WIN32)
    /**
     * @brief Construct a new Poller object from socket
     * 
     * @param ctxt The IoContext to use
     * @param sockfd The socket to poll
     */
    Poller(IoContext &ctxt, socket_t sockfd) : Poller(ctxt, fd_t(sockfd), IoDescriptor::Socket) { }
#endif // defined(_WIN32)

    Poller(const Poller &) = delete;

    /**
     * @brief Construct a new Poller object by moving from another
     * 
     * @param other 
     */
    Poller(Poller &&other) : 
        mCtxt(std::exchange(other.mCtxt, nullptr)), 
        mDesc(std::exchange(other.mDesc, nullptr)), 
        mFd(std::exchange(other.mFd, fd_t{ })) 
    {

    }

    ~Poller() {
        close();
    }

    /**
     * @brief Close the current poller
     * 
     */
    auto close() -> void {
        if (mDesc) {
            mCtxt->removeDescriptor(mDesc);
            mDesc = nullptr;
        }
        mCtxt = nullptr;
        mFd = { };
    }

    /**
     * @brief Poll for events on the fd
     * 
     * @param events The events to poll for (like PollEvent::In | PollEvent::Out)
     * @return IoTask<uint32_t> The reveived events
     */
    auto poll(uint32_t events) const -> IoTask<uint32_t> {
        return mCtxt->poll(mDesc, events);
    }

    /**
     * @brief Get the context of the poller
     * 
     * @return IoContext* 
     */
    auto context() const -> IoContext * {
        return mCtxt;
    }

    /**
     * @brief Get the fd of the poller
     * 
     * @return fd_t 
     */
    auto fd() const -> fd_t {
        return mFd;
    }
private:
    IoContext    *mCtxt = nullptr;
    IoDescriptor *mDesc = nullptr;
    fd_t          mFd { };
};

ILIAS_NS_END