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
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/net/msg.hpp>
#include <ilias/log.hpp>
#include <QSocketNotifier>
#include <QMetaObject>
#include <QTimerEvent>
#include <QEventLoop>
#include <QMetaEnum>
#include <QObject>
#include <map>

#if defined(_WIN32)
    #include <ilias/platform/detail/iocp_sock.hpp> // For extended socket functions
    #include <ilias/platform/detail/iocp_fs.hpp> // For read console
    #include <QWinEventNotifier>
#endif // defined(_WIN32)

#if defined(__linux__) && __has_include(<aio.h>)
    #include <ilias/platform/detail/aio_core.hpp> // For read file
#endif


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
    ~QIoDescriptor() { }

    union {
        fd_t     fd;                    //< Platform's fd
        socket_t sockfd = socket_t(-1); //< Platform's socket fd
    };

    IoDescriptor::Type type = Unknown;
    bool pollable = false; //< true on we can use QSocketNotifier to poll

    struct { //< For Pollable
        QSocketNotifier *readNotifier = nullptr;
        QSocketNotifier *writeNotifier = nullptr;
        QSocketNotifier *exceptNotifier = nullptr;
        size_t           numOfRead = 0;
        size_t           numOfWrite = 0;
        size_t           numOfExcept = 0;
    } poll;

#if defined(_WIN32)
    struct {
        LPFN_WSASENDMSG sendmsg = nullptr;
        LPFN_WSARECVMSG recvmsg = nullptr;
    } sock;
#endif

};

/**
 * @brief The internal QObject::startTimer implementation for sleep(ms) function
 * 
 */
class QTimerAwaiter final {
public:
    QTimerAwaiter(QIoContext *context, uint64_t ms) : mCtxt(context), mMs(ms) { }

    auto await_ready() -> bool { return mMs == 0; }
    auto await_suspend(CoroHandle caller) -> bool;
    auto await_resume() -> Result<void>;
private:
    auto onTimeout() -> void;
    auto onCancel() -> void;

    QIoContext *mCtxt;
    uint64_t   mMs;
    CoroHandle mCaller;
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
    auto await_suspend(CoroHandle caller) -> void;
    auto await_resume() -> Result<uint32_t>;
    auto _trace(CoroHandle caller) -> void;
private:
    auto onNotifierActivated(QSocketDescriptor, QSocketNotifier::Type type) -> void;
    auto onFdDestroyed() -> void;
    auto doDisconnect() -> void;
    auto doConnect() -> void;
    auto onCancel() -> void;

    QIoDescriptor *mFd;
    uint32_t       mEvent;
    CoroHandle     mCaller;
    Result<uint32_t> mResult; // The result of poll function. revent or Error
    QMetaObject::Connection mReadCon;
    QMetaObject::Connection mWriteCon;
    QMetaObject::Connection mExceptCon;
    QMetaObject::Connection mDestroyCon; // To observe the QIoDescriptor destroyed
    CancellationToken::Registration mRegistration;
};

#if defined(_WIN32)
/**
 * @brief The internal qt impl for any using OVERLAPPED
 * 
 */
class QOverlapped {
public:
    QOverlapped(::HANDLE handle) : mHandle(handle) { }
    QOverlapped(const QOverlapped &) = delete;
    ~QOverlapped() { ::CloseHandle(mOverlapped.hEvent); }

    auto await_ready() -> bool { return false; }
    auto await_suspend(CoroHandle caller) -> void;
    auto await_resume() -> void { }
    auto setOffset(uint64_t offset) -> void;
    auto operator &() -> ::OVERLAPPED * { return &mOverlapped; }
private:
    ::OVERLAPPED mOverlapped { .hEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr) };
    ::HANDLE     mHandle;
    CoroHandle   mCaller;
    QWinEventNotifier mNotifier { mOverlapped.hEvent }; // To observe the event
    CancellationToken::Registration mRegistration;
}; //TODO: Add Event Pool for reduce the create and destroy event
#endif // defined(_WIN32)

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
    auto sleep(uint64_t ms) -> IoTask<void> override;

    // < For IoContext
    auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> Result<IoDescriptor*> override;
    auto removeDescriptor(IoDescriptor* fd) -> Result<void> override;
    auto cancel(IoDescriptor* fd) -> Result<void> override;

    auto read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> override;
    auto write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> override;

    auto accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> override;
    auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> override;

    auto sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, EndpointView endpoint) -> IoTask<size_t> override;
    auto recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> override;

    auto sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> override;
    auto recvmsg(IoDescriptor *fd, MsgHdr &msg, int flags) -> IoTask<size_t> override;

    auto poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> override;

#if defined(_WIN32)
    auto connectNamedPipe(IoDescriptor *fd) -> IoTask<void> override;
#endif // defined(_WIN32)

protected:
    auto timerEvent(QTimerEvent *event) -> void override;
private:
    auto submitTimer(uint64_t ms, detail::QTimerAwaiter *awaiter) -> int;
    auto cancelTimer(int timerId) -> void;

    SockInitializer mInit;
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
#if !defined(NDEBUG)
        ILIAS_WARN("QIo", "QIoContext::~QIoContext(): dump object tree");
        dumpObjectTree();
#endif
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
    auto nfd = std::make_unique<detail::QIoDescriptor>(this);

    // If the type is unknown, we need to check it
    if (type == IoDescriptor::Unknown) {
        auto ret = fd_utils::type(fd);
        if (!ret) {
            return Unexpected(ret.error());
        }
        type = *ret;
    }
    // Check the fd type that we support
    switch (type) {
        case IoDescriptor::Socket:
            nfd->pollable = true;
            break;

#if   defined(_WIN32)
        case IoDescriptor::Pipe:
        case IoDescriptor::File:
        case IoDescriptor::Tty:
            break;
#elif defined(__linux__)
        case IoDescriptor::Pipe:
            nfd->pollable = true; // Linux pipe can be pollable
            break;
#if __has_include(<aio.h>) // If posix aio available, we can use it
        case IoDescriptor::Tty:
        case IoDescriptor::File:
            break;
#endif

#endif // defined(_WIN32)

        default:
            ILIAS_WARN("QIo", "QIoContext::addDescriptor(): the descriptor type {} is not supported", type);
            return Unexpected(Error::OperationNotSupported);
    }
    
    nfd->type = type;
    nfd->fd = fd;
    // Prepare env for pollable (in windows only socket can be pollable)
    if (nfd->pollable) {
        nfd->poll.readNotifier = new QSocketNotifier(nfd->sockfd, QSocketNotifier::Read, nfd.get());
        nfd->poll.writeNotifier = new QSocketNotifier(nfd->sockfd, QSocketNotifier::Write, nfd.get());
        nfd->poll.exceptNotifier = new QSocketNotifier(nfd->sockfd, QSocketNotifier::Exception, nfd.get());
        nfd->poll.readNotifier->setEnabled(false);
        nfd->poll.writeNotifier->setEnabled(false);
        nfd->poll.exceptNotifier->setEnabled(false);

        // Set nonblock, linux pollable fd can also use this way to set nonblock
        SocketView sockfd(nfd->sockfd);
        if (auto ret = sockfd.setBlocking(false); !ret) {
            return Unexpected(ret.error());
        }
    }

#if defined(_WIN32)
    // Setup option for windows specific socket
    if (nfd->type == IoDescriptor::Socket) {
        // Disable UDP NetReset and ConnReset
        SocketView sockfd(nfd->sockfd);
        if (auto info = sockfd.getOption<sockopt::ProtocolInfo>(); info && info->value().iSocketType == SOCK_DGRAM) {
            if (auto res = sockfd.setOption(sockopt::UdpConnReset(false)); !res) {
                ILIAS_WARN("QIo", "QIoContext::addDescriptor(): failed to disable UDP NetReset, {}", res.error());
            }
            if (auto res = sockfd.setOption(sockopt::UdpNetReset(false)); !res) {
                ILIAS_WARN("QIo", "QIoContext::addDescriptor(): failed to disable UDP ConnReset, {}", res.error());
            }
        }
        // Get the extension of sendmsg and recvmsg
        if (auto res = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSASENDMSG, &nfd->sock.sendmsg); !res) {
            ILIAS_WARN("QIo", "QIoContext::addDescriptor(): failed to get sendmsg extension, {}", res.error());
        }

        if (auto res = detail::WSAGetExtensionFnPtr(nfd->sockfd, WSAID_WSARECVMSG, &nfd->sock.recvmsg); !res) {
            ILIAS_WARN("QIo", "QIoContext::addDescriptor(): failed to get recvmsg extension, {}", res.error());
        }
    }
#endif // defined(_WIN32)

    // Set the debug name
#if !defined(NDEBUG)
    nfd->setObjectName(QString("IliasQIoDescriptor_%1").arg(nfd->sockfd));
#endif // !defined(NDEBUG)

    ++mNumOfDescriptors;
    return nfd.release();
}

inline auto QIoContext::removeDescriptor(IoDescriptor *fd) -> Result<void> {
    if (!fd) {
        return {};
    }
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    cancel(nfd);
    delete nfd;
    --mNumOfDescriptors;
    return {};
}

inline auto QIoContext::cancel(IoDescriptor *fd) -> Result<void> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    
#if defined(_WIN32)
    if (!::CancelIoEx(nfd->fd, nullptr)) {
        if (auto err = ::GetLastError(); err != ERROR_NOT_FOUND) {
            ILIAS_WARN("QIo", "QIoContext::removeDescriptor(): failed to cancel any pending IO operation on {}, {}", nfd->fd, err);            
        }
    }
#endif // defined(_WIN32)

    Q_EMIT nfd->destroyed(); // Using this signal to notify the operation is canceled
    return {};
}

inline auto QIoContext::sleep(uint64_t ms) -> IoTask<void> {
    co_return co_await detail::QTimerAwaiter {this, ms};
}

inline auto QIoContext::read(IoDescriptor *fd, std::span<std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);

#if defined(_WIN32)
    if (nfd->type == IoDescriptor::Tty) {
        co_return co_await detail::IocpThreadReadAwaiter(nfd->fd, buffer);
    }
    if (nfd->type == IoDescriptor::Pipe || nfd->type == IoDescriptor::File) {
        detail::QOverlapped overlapped(nfd->fd);
        ::DWORD bytesRead = 0;
        if (offset) {
            overlapped.setOffset(offset.value());
        }
        if (::ReadFile(nfd->fd, buffer.data(), buffer.size(), &bytesRead, &overlapped)) {
            co_return bytesRead;
        }
        if (auto err = ::GetLastError(); err != ERROR_IO_PENDING) {
            co_return Unexpected(SystemError(err));
        }
        co_await overlapped;
        if (::GetOverlappedResult(nfd, &overlapped, &bytesRead, FALSE)) {
            co_return bytesRead;
        }
        co_return Unexpected(SystemError::fromErrno());
    }
#endif // defined(_WIN32)

#if defined(__linux__)
    if (nfd->type == IoDescriptor::Pipe) {
        while (true) {
            if (auto ret = ::read(nfd->fd, buffer.data(), buffer.size()); ret >= 0) {
                co_return ret;
            }

            // Error Handling
            if (auto err = errno; err != EAGAIN && err != EWOULDBLOCK) {
                co_return Unexpected(SystemError(err));
            }
        }
    }

#if __has_include(<aio.h>)
    if (nfd->type == IoDescriptor::Tty || nfd->type == IoDescriptor::File) {
        co_return co_await detail::AioReadAwaiter(nfd->fd, buffer, offset);
    }
#endif // __has_include(<aio.h>)

#endif // defined(__linux__)

    if (nfd->type == IoDescriptor::Socket) {
        co_return co_await recvfrom(fd, buffer, 0, nullptr);
    }
    co_return Unexpected(Error::OperationNotSupported);
}

inline auto QIoContext::write(IoDescriptor *fd, std::span<const std::byte> buffer, std::optional<size_t> offset) -> IoTask<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);

#if defined(_WIN32)
    if (nfd->type == IoDescriptor::Tty) {
        co_return co_await detail::IocpThreadWriteAwaiter(nfd->fd, buffer);
    }
    if (nfd->type == IoDescriptor::Pipe || nfd->type == IoDescriptor::File) {
        detail::QOverlapped overlapped(nfd->fd);
        ::DWORD bytesWritten = 0;
        if (offset) {
            overlapped.setOffset(offset.value());
        }
        if (::WriteFile(nfd->fd, buffer.data(), buffer.size(), &bytesWritten, &overlapped)) {
            co_return bytesWritten;
        }
        if (auto err = ::GetLastError(); err != ERROR_IO_PENDING) {
            co_return Unexpected(SystemError(err));
        }
        co_await overlapped;
        if (::GetOverlappedResult(nfd, &overlapped, &bytesWritten, FALSE)) {
            co_return bytesWritten;
        }
        co_return Unexpected(SystemError::fromErrno());
    }
#endif // defined(_WIN32)

#if defined(__linux__)
    if (nfd->type == IoDescriptor::Pipe) {
        while (true) {
            if (auto ret = ::write(nfd->fd, buffer.data(), buffer.size()); ret >= 0) {
                co_return ret;
            }
            // Error Handling
            if (auto err = errno; err != EAGAIN && err != EWOULDBLOCK) {
                co_return Unexpected(SystemError::fromErrno());
            }
            if (auto pollret = co_await poll(fd, PollEvent::Out); !pollret) {
                co_return Unexpected(pollret.error());
            }
        }
    }
#if __has_include(<aio.h>)
    if (nfd->type == IoDescriptor::Tty || nfd->type == IoDescriptor::File) {
        co_return co_await detail::AioWriteAwaiter(nfd->fd, buffer, offset);
    }
#endif // __has_include(<aio.h>)

#endif // defined(__linux__)

    if (nfd->type == IoDescriptor::Socket) {
        co_return co_await sendto(fd, buffer, 0, nullptr);
    }
    co_return Unexpected(Error::OperationNotSupported);
}

inline auto QIoContext::accept(IoDescriptor *fd, MutableEndpointView endpoint) -> IoTask<socket_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sock = SocketView(nfd->sockfd);
    while (true) {
        auto ret = sock.accept<socket_t>(endpoint);
        if (ret) {
            co_return *ret;
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

inline auto QIoContext::connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> {
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
            co_return Unexpected(pollret.error());
        }
        auto err = sock.error().value();
        if (err.isOk()) {
            co_return {};
        }
        co_return Unexpected(err);
    }
}

inline auto QIoContext::sendto(IoDescriptor *fd, std::span<const std::byte> buffer, int flags, EndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sock = SocketView(nfd->sockfd);
    while (true) {
        auto ret = sock.sendto(buffer, flags, endpoint);
        if (ret) {
            co_return *ret;
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

inline auto QIoContext::recvfrom(IoDescriptor *fd, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sock = SocketView(nfd->sockfd);
    while (true) {
        auto ret = sock.recvfrom(buffer, flags, endpoint);
        if (ret) {
            co_return *ret;
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

inline auto QIoContext::sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto sendmsg = [&](socket_t sockfd, const MsgHdr &msg, int flags) -> Result<size_t> {

#if defined(_WIN32)
        if (!nfd->sock.sendmsg) {
            return Unexpected(Error::OperationNotSupported);
        }
        auto msgcpy = msg; // Copy the message to avoid modifying the original
        DWORD bytesSent = 0;
        if (nfd->sock.sendmsg(sockfd, &msgcpy, flags, &bytesSent, nullptr, nullptr) == 0) {
            return bytesSent;
        }
#else
        if (auto ret = ::sendmsg(sockfd, &msg, flags); ret >= 0) {
            return ret;
        }
#endif // defined(_WIN32)
        return Unexpected(SystemError::fromErrno());
    };

    while (true) {
        auto ret = sendmsg(nfd->sockfd, msg, flags);
        if (ret) {
            co_return *ret;
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

inline auto QIoContext::recvmsg(IoDescriptor *fd, MsgHdr &msg, int flags) -> IoTask<size_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    auto recvmsg = [&](socket_t sockfd, MsgHdr &msg, int flags) -> Result<size_t> {

#if defined(_WIN32)
        if (!nfd->sock.recvmsg) {
            return Unexpected(Error::OperationNotSupported);
        }
        DWORD bytesReceived = 0;
        msg.dwFlags = flags;
        if (nfd->sock.recvmsg(sockfd, &msg, &bytesReceived, nullptr, nullptr) == 0) {
            return bytesReceived;
        }
#else
        if (auto ret = ::recvmsg(sockfd, &msg, flags); ret >= 0) {
            return ret;
        }
#endif // defined(_WIN32)
        return Unexpected(SystemError::fromErrno());
    };

    while (true) {
        auto ret = recvmsg(nfd->sockfd, msg, flags);
        if (ret) {
            co_return *ret;
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

inline auto QIoContext::poll(IoDescriptor *fd, uint32_t event) -> IoTask<uint32_t> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    if (!nfd->pollable) {
        co_return Unexpected(Error::OperationNotSupported);
    }
    co_return co_await detail::QPollAwaiter {nfd, event};
}

#if defined(_WIN32)
inline auto QIoContext::connectNamedPipe(IoDescriptor *fd) -> IoTask<void> {
    auto nfd = static_cast<detail::QIoDescriptor*>(fd);
    detail::QOverlapped overlapped(nfd->fd);
    if (!::ConnectNamedPipe(nfd->fd, &overlapped)) {
        if (auto err = ::GetLastError(); err != ERROR_IO_PENDING) {
            co_return Unexpected(SystemError(err));
        }
        co_await overlapped;
        if (!::GetOverlappedResult(nfd->fd, &overlapped, nullptr, FALSE)) {
            co_return Unexpected(SystemError::fromErrno());
        }
    }
    co_return {};
}
#endif // defined(_WIN32)

// Timer
inline auto QIoContext::timerEvent(QTimerEvent *event) -> void {
    auto iter = mTimers.find(event->timerId());
    if (iter == mTimers.end()) {
        ILIAS_WARN("QIo", "Timer {} not found", event->timerId());
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
    return id;
}

inline auto QIoContext::cancelTimer(int id) -> void {
    if (id == 0) {
        return;
    }
    killTimer(id);
    mTimers.erase(id);
}

inline auto detail::QTimerAwaiter::await_suspend(CoroHandle caller) -> bool {
    mCaller = caller;
    mTimerId = mCtxt->submitTimer(mMs, this);
    mRegistration = caller.cancellationToken().register_(std::bind(&QTimerAwaiter::onCancel, this));
    if (mTimerId == 0) {
        ILIAS_WARN("QIo", "Timer could not be created");
    }
    return mTimerId != 0; // If the timer was not created, we can't suspend
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
inline auto detail::QPollAwaiter::await_suspend(CoroHandle caller) -> void {
    ILIAS_TRACE("QIo", "poll fd {} for event {}", mFd->sockfd, PollEvent(mEvent));
    mCaller = caller;
    doConnect(); //< Connect the signal
    mRegistration = caller.cancellationToken().register_(std::bind(&QPollAwaiter::onCancel, this));
}

inline auto detail::QPollAwaiter::await_resume() -> Result<uint32_t> {
    ILIAS_ASSERT(!mReadCon && !mWriteCon && !mExceptCon && !mDestroyCon);
    return mResult;
}

#if defined(ILIAS_TASK_TRACE)
inline auto detail::QPollAwaiter::_trace(CoroHandle caller) -> void {
    caller.frame().msg = fmtlib::format("poll fd {} for event {}", mFd->sockfd, PollEvent(mEvent));
}
#endif

inline auto detail::QPollAwaiter::onCancel() -> void {
    ILIAS_TRACE("QIo", "poll fd {} was canceled", mFd->sockfd);
    doDisconnect();
    mResult = Unexpected(Error::Canceled);
    mCaller.schedule();
}

inline auto detail::QPollAwaiter::onFdDestroyed() -> void {
    ILIAS_TRACE("QIo", "fd {} was destroyed", mFd->sockfd);
    doDisconnect();
    mResult = Unexpected(Error::Canceled);
    mCaller.schedule();
}

inline auto detail::QPollAwaiter::onNotifierActivated(QSocketDescriptor, QSocketNotifier::Type type) -> void {
    auto type2str = [](QSocketNotifier::Type type) {
        switch (type) {
            case QSocketNotifier::Read: return "Read";
            case QSocketNotifier::Write: return "Write";
            case QSocketNotifier::Exception: return "Exception";
            default: return "Unknown";
        }
    };

    ILIAS_TRACE("QIo", "fd {} was activated by {}", mFd->sockfd, type2str(type));
    doDisconnect();
    if (type == QSocketNotifier::Read) {
        mResult = PollEvent::In;
    }
    else if (type == QSocketNotifier::Write) {
        mResult = PollEvent::Out;
    }
    else {
        mResult = PollEvent::Hup;
    }
    mCaller.schedule();
}

inline auto detail::QPollAwaiter::doDisconnect() -> void {
    if (mReadCon) {
        mFd->poll.readNotifier->disconnect(mReadCon);
        mFd->poll.numOfRead--;
    }
    if (mWriteCon) {
        mFd->poll.writeNotifier->disconnect(mWriteCon);
        mFd->poll.numOfWrite--;
    }
    if (mExceptCon) {
        mFd->poll.exceptNotifier->disconnect(mExceptCon);
        mFd->poll.numOfExcept--;
    }
    if (mDestroyCon) {
        mFd->disconnect(mDestroyCon);
    }

    // Check the num of connections, and disable it if 0
    if (mFd->poll.numOfRead == 0) {
        mFd->poll.readNotifier->setEnabled(false);
    }
    if (mFd->poll.numOfWrite == 0) {
        mFd->poll.writeNotifier->setEnabled(false);
    }
    if (mFd->poll.numOfExcept == 0) {
        mFd->poll.exceptNotifier->setEnabled(false);
    }
}

inline auto detail::QPollAwaiter::doConnect() -> void {
    auto fn = std::bind(&QPollAwaiter::onNotifierActivated, this, std::placeholders::_1, std::placeholders::_2);
    auto destroyFn = std::bind(&QPollAwaiter::onFdDestroyed, this);
    if (mEvent & PollEvent::In) {
        mReadCon = QObject::connect(mFd->poll.readNotifier, &QSocketNotifier::activated, fn);
        mFd->poll.readNotifier->setEnabled(true);
        mFd->poll.numOfRead++;
    }
    if (mEvent & PollEvent::Out) {
        mWriteCon = QObject::connect(mFd->poll.writeNotifier, &QSocketNotifier::activated, fn);
        mFd->poll.writeNotifier->setEnabled(true);
        mFd->poll.numOfWrite++;
    }
    // Connect the except notifier
    mExceptCon = QObject::connect(mFd->poll.exceptNotifier, &QSocketNotifier::activated, fn);
    mFd->poll.exceptNotifier->setEnabled(true);
    mFd->poll.numOfExcept++;

    // Connect the destroy notifier
    mDestroyCon = QObject::connect(mFd, &QObject::destroyed, destroyFn);
}

#if defined(_WIN32)
inline auto detail::QOverlapped::await_suspend(CoroHandle caller) -> void {
    mCaller = caller;
    mRegistration = caller.cancellationToken().register_([this]() {
        ::CancelIoEx(mHandle, &mOverlapped);
    });
    QObject::connect(&mNotifier, &QWinEventNotifier::activated, [this](::HANDLE event) {
        mNotifier.setEnabled(false);
        mCaller.schedule();
    });
    mNotifier.setEnabled(true);
}

inline auto detail::QOverlapped::setOffset(size_t offset) -> void {
    ::ULARGE_INTEGER integer {
        .QuadPart = offset
    };
    mOverlapped.Offset = integer.LowPart;
    mOverlapped.OffsetHigh = integer.HighPart;
}
#endif // defined(_WIN32)

ILIAS_NS_END