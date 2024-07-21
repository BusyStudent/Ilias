#pragma once

#include "backend.hpp"

ILIAS_NS_BEGIN

/**
 * @brief A Helper class for poll socket fd
 * 
 */
class Poller {
public:
    Poller(IoContext &ctxt, socket_t fd);
    Poller(const Poller &) = delete;
    ~Poller();

    auto poll(uint32_t event) -> Task<uint32_t>;
private:
    IoContext &mCtxt;
    socket_t   mFd;
};

inline Poller::Poller(IoContext &ctxt, socket_t fd) : mCtxt(ctxt), mFd(fd) {
    mCtxt.addSocket(mFd).value();
}
inline Poller::~Poller() { 
    mCtxt.removeSocket(mFd).value();
}

inline auto Poller::poll(uint32_t event) -> Task<uint32_t> { 
    return mCtxt.poll(mFd, event); 
}

ILIAS_NS_END