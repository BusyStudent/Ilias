/**
 * @file epoll_event.hpp
 * @author llhsdmd (llhsdmd@gmail.com)
 * @brief Provides a public api set for Epoll Interop.
 * @version 0.1
 * @date 2024-09-03
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

#include <sys/epoll.h>
#include <memory>
#include <cstring>
#include <unordered_set>

#include <ilias/cancellation_token.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN
namespace detail {
class EpollAwaiter;

std::string toString(uint32_t events) {
    std::string ret = "";
    if (events & EPOLLIN) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLIN";
    }
    if (events & EPOLLOUT) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLOUT";
    }
    if (events & EPOLLRDHUP) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLRDHUP";
    }
    if (events & EPOLLERR) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLERR";
    }
    if (events & EPOLLHUP) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLHUP";
    }
    if (events & EPOLLET) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLET";
    }
    if (events & EPOLLONESHOT) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLONESHOT";
    }
    if (events & EPOLLWAKEUP) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLWAKEUP";
    }
    if (events & EPOLLEXCLUSIVE) {
        ret += ret.empty() ? "" : " | ";
        ret += "EPOLLEXCLUSIVE";
    }
    return ret;
}

struct EpollEvent {
    int   fd        = 0;
    int   epollfd   = 0;
    void *data      = nullptr;
    bool  isResumed = false;
};
/**
 * @brief poll for Epoll Awatier.
 *
 */
class EpollAwaiter {
public:
    EpollAwaiter(EpollEvent &epollEvent, uint32_t events) : mEpollEvent(epollEvent), mEvents(events) {}

    auto await_ready() -> bool {
        epoll_event event;
        ::std::memset(&event, 0, sizeof(event));
        event.data.fd    = mEpollEvent.fd;
        event.events     = mEvents;
        mEpollEvent.data = this;
        auto ret         = 0;
        ret              = ::epoll_ctl(mEpollEvent.epollfd, EPOLL_CTL_MOD, mEpollEvent.fd, &event);
        if (ret == -1) {
            ILIAS_ERROR("Epoll", "epoll_ctl {} error: {}", mEpollEvent.fd, strerror(errno));
            mEpollError = errno;
            return true;
        }
        ILIAS_TRACE("Epoll", "ready awaiter<{}> event({}) on fd({})", (void *)this, toString(event.events),
                    mEpollEvent.fd);
        return false; //< Wating Epoll
    }
    auto await_suspend(TaskView<> caller) -> void {
        ILIAS_TRACE("Epoll", "suspend awaiter<{}> event({}) on fd({})", (void *)this, toString(mEvents),
                    mEpollEvent.fd);
        mCaller       = caller;
        mRegistration = mCaller.cancellationToken().register_(onCancel, this);
    }

    auto await_resume() -> Result<uint32_t> {
        if (mIsCancelled) {
            ILIAS_TRACE("Epoll", "awaiter<{}> is cancelled", (void *)this);
            return Unexpected<Error>(Error::Canceled);
        }
        if (mEpollError != 0) {
            ILIAS_ERROR("Epoll", "awaiter<{}> has error: {}", (void *)this, SystemError(mEpollError).toString());
            return Unexpected<Error>(SystemError(mEpollError));
        }
        ILIAS_TRACE("Epoll", "resume awaiter<{}> event({}) on fd({})", (void *)this, toString(mEvents), mEpollEvent.fd);
        return mRevents;
    }

    inline static auto onCompletion(uint32_t revents, void *data) -> void {
        auto self = static_cast<EpollAwaiter *>(data);
        if (self == nullptr || self->mEpollEvent.isResumed) {
            ILIAS_ERROR("Epoll", "awaiter<{}> already resumed", data);
            return;
        }
        self->mEpollEvent.isResumed = true;
        self->mRevents              = revents;
        self->mCaller.resume();
    }

    inline static auto onCancel(void *data) -> void {
        auto self = static_cast<EpollAwaiter *>(data);
        ILIAS_TRACE("Epoll", "awaiter<{}> cancel, evnt({}) on fd({})", data, toString(self->mEvents),
                    self->mEpollEvent.fd);
        if (self == nullptr || self->mEpollEvent.isResumed) {
            ILIAS_ERROR("Epoll", "awaiter<{}> already resumed", data);
            return;
        }
        self->mIsCancelled = true;
        self->mCaller.schedule();
    }

private:
    int                             mEpollError  = 0; //< Does we got any error from add the fd
    uint32_t                        mRevents     = 0; //< Received events
    uint32_t                        mEvents      = 0; //< Events to wait for
    bool                            mIsCancelled = false;
    TaskView<>                      mCaller;
    CancellationToken::Registration mRegistration;
    EpollEvent                     &mEpollEvent;
};

} // namespace detail

ILIAS_NS_END
