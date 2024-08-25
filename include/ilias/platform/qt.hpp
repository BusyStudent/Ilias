/**
 * @file qt.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides Qt support
 * @version 0.1
 * @date 2024-08-25
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/cancellation_token.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/io/context.hpp>
#include <ilias/log.hpp>
#include <QSocketNotifier>
#include <QMetaObject>
#include <QTimerEvent>
#include <QEventLoop>
#include <QObject>
#include <map>

ILIAS_NS_BEGIN

class QIoContext;

namespace detail {

/**
 * @brief The Qt's Descriptor implementation
 * 
 */
class QIoDescriptor final : public IoDescriptor, public QObject {
public:
    QIoDescriptor(QObject *parent = nullptr) : QObject(parent) { }
    QIoDescriptor(const QIoDescriptor &) = delete;
    ~QIoDescriptor() { Q_EMIT destroyed(); }

    union {
        fd_t    fd;          //< Platform's fd
        qintptr sockfd = -1; //< Platform's socket fd
    };

    IoDescriptor::Type type = Unknown;
    bool pollable = false; //< true on we can use QSocketNotifier to poll

    struct { //< For Socket
        QSocketNotifier *readNotifier = nullptr;
        QSocketNotifier *writeNotifier = nullptr;
        QSocketNotifier *exceptNotifier = nullptr;
        size_t           numOfRead = 0;
        size_t           numOfWrite = 0;
        size_t           numOfExcept = 0;
    };
};

/**
 * @brief The internal QObject::startTimer implementation for sleep(ms) function
 * 
 */
class QTimerAwaiter final {
public:
    QTimerAwaiter(QIoContext *context, uint64_t ms) : mCtxt(context), mMs(ms) { }

    auto await_ready() -> bool { return mMs == 0; }
    auto await_suspend(TaskView<> caller) -> void;
    auto await_resume() -> Result<void>;
private:
    auto onTimeout() -> void;
    auto onCancel() -> void;

    QIoContext *mCtxt;
    uint64_t   mMs;
    TaskView<> mCaller;
    int        mTimerId = 0;
    bool       mCanceled = false;
    CancellationToken::Registration mRegistration;
friend class QIoContext;
};

/**
 * @brief Impl the QIoContext::poll(fd, event) function
 * 
 */
class QPollAwaiter {
public:
    QPollAwaiter(QIoDescriptor *fd, uint32_t event) : mFd(fd), mEvent(event) { }

    auto await_ready() -> bool { return false; }
    auto await_suspend(TaskView<> caller) -> void;
    auto await_resume() -> Result<uint32_t>;
private:
    auto onNotifierActivated(QSocketDescriptor, QSocketNotifier::Type type) -> void;
    auto onFdDestroyed() -> void;
    auto doDisconnect() -> void;
    auto doConnect() -> void;
    auto onCancel() -> void;

    QIoDescriptor *mFd;
    uint32_t       mEvent;
    TaskView<>     mCaller;
    Result<uint32_t> mResult; //< The result of poll function. revent or Error
    QMetaObject::Connection mReadCon;
    QMetaObject::Connection mWriteCon;
    QMetaObject::Connection mExceptCon;
    QMetaObject::Connection mDestroyCon; //< to observe the QIoDescriptor destroyed
    CancellationToken::Registration mRegistration;
};

} // namespace detail


/**
 * @brief The Qt IoContext implementation, it need a qt's event loop to run
 * 
 */
class QIoContext final : public IoContext, public QObject {
public:
    QIoContext(QObject *parent = nullptr);
    QIoContext(const QIoContext &) = delete;
    ~QIoContext();

    //< For Executor
    auto post(void (*fn)(void *), void *args) -> void override;
    auto run(CancellationToken &token) -> void override;
    auto sleep(uint64_t ms) -> Task<void> override;

    // < For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> Result<void> override;

    auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> override;
    auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> override;

    auto accept(IoDescriptor *fd, IPEndpoint *endpoint) -> Task<socket_t> override;
    auto connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> override;

    auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, const IPEndpoint *endpoint) -> Task<size_t> override;
    auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, IPEndpoint *endpoint) -> Task<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> override;
protected:
    auto timerEvent(QTimerEvent *event) -> void override;
private:
    auto submitTimer(uint64_t ms, detail::QTimerAwaiter *awaiter) -> int;
    auto cancelTimer(int timerId) -> void;

    size_t mNumOfDescriptors = 0; //< How many descriptors are added
    std::map<int, detail::QTimerAwaiter *> mTimers; //< Timer map
friend class detail::QTimerAwaiter;
};

inline QIoContext::QIoContext(QObject *parent) : QObject(parent) {
    setObjectName("IliasQIoContext");
}

inline QIoContext::~QIoContext() {
    if (mNumOfDescriptors > 0) {
        ILIAS_ERROR("QIo", "QIoContext::~QIoContext(): descriptors are not removed, {} exist", mNumOfDescriptors);
    }
}

inline auto QIoContext::post(void (*fn)(void *), void *args) -> void {
    QMetaObject::invokeMethod(this, [=]() {
        fn(args);
    }, Qt::QueuedConnection);
}

inline auto QIoContext::run(CancellationToken &token) -> void {
    QEventLoop loop;
    auto reg = token.register_([&]() {
        loop.quit();
    });
    loop.exec();
}

inline auto QIoContext::addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> {
    if (type != IoDescriptor::Socket) {
        ILIAS_WARN("QIo", "QIoContext::addDescriptor(): currently only support Socket type");
        return Unexpected(Error::OperationNotSupported);
    }
    auto nfd = std::make_unique<detail::QIoDescriptor>(this);
    nfd->type = type;

    // Check if fd is pollable
    switch (type) {
        case IoDescriptor::Socket:
            nfd->pollable = true;
            break;
        default:
            break;
    }
    // Prepare env for Socket
    if (nfd->pollable) {
        nfd->sockfd = qintptr(fd);
        nfd->readNotifier = new QSocketNotifier(QSocketNotifier::Read, nfd.get());
        nfd->writeNotifier = new QSocketNotifier(QSocketNotifier::Write, nfd.get());
        nfd->exceptNotifier = new QSocketNotifier(QSocketNotifier::Exception, nfd.get());
        nfd->readNotifier->setEnabled(false);
        nfd->writeNotifier->setEnabled(false);
        nfd->exceptNotifier->setEnabled(false);
    }

    ++mNumOfDescriptors;
    return nfd.release();
}

inline auto QIoContext::removeDescriptor(IoDescriptor *fd) -> Result<void> {
    if (!fd) {
        return {};
    }
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    delete nfd;
    --mNumOfDescriptors;
    return {};
}

inline auto QIoContext::sleep(uint64_t ms) -> Task<void> {
    co_return co_await detail::QTimerAwaiter {this, ms};
}

inline auto QIoContext::read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Socket) {
        co_return co_await sendto(fd, buffer, 0, nullptr);
    }
    co_return Unexpected(Error::OperationNotSupported);
}

inline auto QIoContext::write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> Task<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    if (nfd->type == IoDescriptor::Socket) {
        co_return co_await sendto(fd, buffer, 0, nullptr);
    }
    co_return Unexpected(Error::OperationNotSupported);
}

inline auto QIoContext::accept(IoDescriptor *fd, IPEndpoint *endpoint) -> Task<socket_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sock = SocketView(nfd->sockfd);
    while (true) {
        auto ret = sock.accept<socket_t>();
        if (ret) {
            auto [fd, addr] = ret.value();
            if (endpoint) {
                *endpoint = addr;
            }
            co_return fd;
        }
        // Error Handling
        if (ret.error() != Error::WouldBlock) {
            co_return Unexpected(ret.error());
        }
        if (auto pollret = co_await poll(fd, PollEvent::In); !pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}

inline auto QIoContext::connect(IoDescriptor *fd, const IPEndpoint &endpoint) -> Task<void> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sock = SocketView(nfd->sockfd);
    while (true) {
        auto ret = sock.connect(endpoint);
        if (ret) {
            co_return {};
        }
        // Error Handling
        if (ret.error() != Error::WouldBlock && ret.error() != Error::InProgress) {
            co_return Unexpected(ret.error());
        }
        auto pollret = co_await poll(fd, PollEvent::Out);
        if (!pollret) {
            co_return Unexpected(ret.error());
        }
        auto err = sock.error().value();
        if (err.isOk()) {
            co_return {};
        }
        co_return Unexpected(err);
    }
}

inline auto QIoContext::sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, const IPEndpoint *endpoint) -> Task<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sock = SocketView(nfd->sockfd);
    while (true) {
        auto ret = sock.sendto(buffer, flags, endpoint);
        if (ret) {
            co_return ret.value();
        }
        // Error Handling
        if (ret.error() != Error::WouldBlock) {
            co_return Unexpected(ret.error());
        }
        if (auto pollret = co_await poll(fd, PollEvent::Out); !pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}

inline auto QIoContext::recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, IPEndpoint *endpoint) -> Task<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sock = SocketView(nfd->sockfd);
    while (true) {
        auto ret = sock.recvfrom(buffer, flags, endpoint);
        if (ret) {
            co_return ret.value();
        }
        // Error Handling
        if (ret.error() != Error::WouldBlock) {
            co_return Unexpected(ret.error());
        }
        if (auto pollret = co_await poll(fd, PollEvent::In); !pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}

inline auto QIoContext::poll(IoDescriptor *fd, uint32_t event) -> Task<uint32_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    if (!nfd->pollable) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::QPollAwaiter {nfd, event};
}

// Timer
inline auto QIoContext::timerEvent(QTimerEvent *event) -> void {
    auto iter = mTimers.find(event->timerId());
    if (iter == mTimers.end()) {
        return;
    }
    auto [id, awaiter] = *iter;
    killTimer(id);
    awaiter->onTimeout();
    mTimers.erase(iter);
}

inline auto QIoContext::submitTimer(uint64_t timeout, detail::QTimerAwaiter *awaiter) -> int {
    auto id = startTimer(std::chrono::milliseconds(timeout));
    if (id == 0) {
        return 0;
    }
    mTimers.emplace(id, awaiter);
}

inline auto QIoContext::cancelTimer(int id) -> void {
    if (id == 0) {
        return;
    }
    killTimer(id);
    mTimers.erase(id);
}

inline auto detail::QTimerAwaiter::await_suspend(TaskView<> caller) -> void {
    mCaller = caller;
    mTimerId = mCtxt->submitTimer(mMs, this);
    mRegistration = caller.cancellationToken().register_(std::bind(&QTimerAwaiter::onCancel, this));
}

inline auto detail::QTimerAwaiter::await_resume() -> Result<void> {
    ILIAS_ASSERT(mTimerId == 0);
    if (mCanceled) {
        return Unexpected(Error::Canceled);
    }
    return {};
}

inline auto detail::QTimerAwaiter::onCancel() -> void {
    if (mTimerId == 0) {
        return;
    }
    mCtxt->cancelTimer(mTimerId);
    mCanceled = true;
    mTimerId = 0;
    mCaller.schedule();
}

inline auto detail::QTimerAwaiter::onTimeout() -> void {
    mTimerId = 0;
    mCaller.schedule();
}

// Poll
inline auto detail::QPollAwaiter::await_suspend(TaskView<> caller) -> void {
    mCaller = caller;
    doConnect(); //< Connect the signal
    mRegistration = caller.cancellationToken().register_(std::bind(&QPollAwaiter::onCancel, this));
}

inline auto detail::QPollAwaiter::await_resume() -> Result<uint32_t> {
    ILIAS_ASSERT(!mReadCon && !mWriteCon && !mExceptCon && !mDestroyCon);
    return mResult;
}

inline auto detail::QPollAwaiter::onCancel() -> void {
    doDisconnect();
    mCaller.schedule();
}

inline auto detail::QPollAwaiter::onFdDestroyed() -> void {
    doDisconnect();
    mResult = Unexpected(Error::Canceled);
    mCaller.schedule();
}

inline auto detail::QPollAwaiter::onNotifierActivated(QSocketDescriptor, QSocketNotifier::Type type) -> void {
    doDisconnect();
    if (type == QSocketNotifier::Read) {
        mResult = PollEvent::In;
    }
    else if (type == QSocketNotifier::Write) {
        mResult = PollEvent::Out;
    }
    else {
        mResult = PollEvent::Err;
    }
    mCaller.schedule();
}

inline auto detail::QPollAwaiter::doDisconnect() -> void {
    if (mReadCon) {
        mFd->readNotifier->disconnect(mReadCon);
        mFd->numOfRead--;
    }
    if (mWriteCon) {
        mFd->writeNotifier->disconnect(mWriteCon);
        mFd->numOfWrite--;
    }
    if (mExceptCon) {
        mFd->exceptNotifier->disconnect(mExceptCon);
        mFd->numOfExcept--;
    }
    if (mDestroyCon) {
        mFd->disconnect(mDestroyCon);
    }

    // Check the num of connections, and disable it if 0
    if (mFd->numOfRead == 0) {
        mFd->readNotifier->setEnabled(false);
    }
    if (mFd->numOfWrite == 0) {
        mFd->writeNotifier->setEnabled(false);
    }
    if (mFd->numOfExcept == 0) {
        mFd->exceptNotifier->setEnabled(false);
    }
}

inline auto detail::QPollAwaiter::doConnect() -> void {
    auto fn = std::bind(&QPollAwaiter::onNotifierActivated, this, std::placeholders::_1, std::placeholders::_2);
    auto destroyFn = std::bind(&QPollAwaiter::onFdDestroyed, this);
    if (mEvent & PollEvent::In) {
        mReadCon = QObject::connect(mFd->readNotifier, &QSocketNotifier::activated, fn);
        mFd->readNotifier->setEnabled(true);
        mFd->numOfRead++;
    }
    if (mEvent & PollEvent::Out) {
        mWriteCon = QObject::connect(mFd->writeNotifier, &QSocketNotifier::activated, fn);
        mFd->writeNotifier->setEnabled(true);
        mFd->numOfWrite++;
    }
    // Connect the except notifier
    mExceptCon = QObject::connect(mFd->exceptNotifier, &QSocketNotifier::activated, fn);
    mFd->exceptNotifier->setEnabled(true);
    mFd->numOfExcept++;

    // Connect the destroy notifier
    mDestroyCon = QObject::connect(mFd, &QObject::destroyed, destroyFn);
}

ILIAS_NS_END