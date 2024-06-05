#pragma once

#include "ilias_backend.hpp"
#include <QSocketNotifier>
#include <QTimerEvent>
#include <QEventLoop>
#include <QObject>
#include <optional>

ILIAS_NS_BEGIN

/**
 * @brief A Io Context compat with Qt's EventLoop
 * 
 */
class QIoContext final : public IoContext, public QObject {
public:
    QIoContext(QObject *parent = nullptr);
    QIoContext(const QIoContext &) = delete;
    ~QIoContext();

    // EventLoop
    auto run(StopToken &token) -> void override;
    auto post(void (*)(void *), void *) -> void override;
    auto delTimer(uintptr_t timer) -> bool override;
    auto addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) -> uintptr_t override;

    // IoContext
    auto addSocket(SocketView fd) -> Result<void> override;
    auto removeSocket(SocketView fd) -> Result<void> override;
    
    auto send(SocketView fd, const void *buffer, size_t n) -> Task<size_t> override;
    auto recv(SocketView fd, void *buffer, size_t n) -> Task<size_t> override;
    auto connect(SocketView fd, const IPEndpoint &endpoint) -> Task<void> override;
    auto accept(SocketView fd) -> Task<std::pair<Socket, IPEndpoint> > override;
    auto sendto(SocketView fd, const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> override;
    auto recvfrom(SocketView fd, void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > override;

    // Poll
    auto poll(SocketView fd, uint32_t events) -> Task<uint32_t> override;
protected:
    auto timerEvent(QTimerEvent *event) -> void override;
private:
    // Wrapping Qt 
    struct Notifier : QObject {
        Notifier(QObject *parent, qintptr fd);
        Notifier(const Notifier &) = delete;
        ~Notifier();

        QSocketNotifier read;
        QSocketNotifier write;
        QSocketNotifier exception;
        size_t numOfRead = 0; //< When 0 it will disable the read notifier
        size_t numOfWrite = 0;
        size_t numOfException = 0;
    };
    // Timers
    struct Timer {
        uintptr_t id; //< TimerId
        int64_t ms;  //< Interval in milliseconds
        int flags;    //< Timer flags
        void (*fn)(void *);
        void *arg;
    };

    SockInitializer mInitializer;
    std::map<qintptr, Notifier *> mFds; //< Mapping fds
    std::map<uintptr_t, Timer> mTimers; //< Timers
};

inline QIoContext::QIoContext(QObject *parent) : QObject(parent) {
    setObjectName("IliasQIoContext");
}
inline QIoContext::~QIoContext() {
    
}
inline auto QIoContext::run(StopToken &token) -> void {
    QEventLoop loop;
    token.setCallback([](void *loop) {
        static_cast<QEventLoop *>(loop)->quit();
    }, &loop);
    loop.exec();
}
inline auto QIoContext::post(void (*func)(void *) , void *data) -> void {
    QMetaObject::invokeMethod(this, [=]() {
        func(data);        
    }, Qt::QueuedConnection);
}

// Timer
inline auto QIoContext::timerEvent(QTimerEvent *event) -> void {
    auto id = event->timerId();
    auto iter = mTimers.find(id);
    if (iter == mTimers.end()) {
        return;
    }
    auto timer = iter->second;
    if (timer.flags & EventLoop::TimerSingleShot) {
        killTimer(id);
        mTimers.erase(iter);
    }
    timer.fn(timer.arg);
}
inline auto QIoContext::delTimer(uintptr_t timer) -> bool {
    auto iter = mTimers.find(timer);
    if (iter == mTimers.end()) {
        return false;
    }
    killTimer(iter->first);
    mTimers.erase(iter);
    return true;
}
inline auto QIoContext::addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) -> uintptr_t {
    auto id = startTimer(ms);
    mTimers.insert(std::make_pair(id,  Timer {uintptr_t(id), ms, flags, fn, arg}));
    return id;
}

// Io
inline auto QIoContext::addSocket(SocketView socket) -> Result<void> {
    if (auto ret = socket.setBlocking(false); !ret) {
        return ret;
    }
    auto notifier = new Notifier(this, socket.get());
    mFds.insert(std::make_pair(socket.get(), notifier));
    return Result<void>();
}
inline auto QIoContext::removeSocket(SocketView socket) -> Result<void> {
    auto iter = mFds.find(socket.get());
    if (iter == mFds.end()) {
        return Unexpected(Error::InvalidArgument);
    }
    delete iter->second;
    mFds.erase(iter);
    return Result<void>();
}

inline auto QIoContext::send(SocketView fd, const void *buffer, size_t n) -> Task<size_t> {
    while (true) {
        auto ret = fd.send(buffer, n);
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd, PollEvent::Out);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto QIoContext::recv(SocketView fd, void *buffer, size_t n) -> Task<size_t> {
    while (true) {
        auto ret = fd.recv(buffer, n);
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd, PollEvent::In);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto QIoContext::connect(SocketView fd, const IPEndpoint &endpoint) -> Task<void> {
    auto ret = fd.connect(endpoint);
    if (ret) {
        co_return ret;
    }
    if (ret.error() != Error::InProgress && ret.error() != Error::WouldBlock) {
        co_return ret;
    }
    auto pollret = co_await poll(fd, PollEvent::Out);
    if (!pollret) {
        co_return Unexpected(pollret.error());
    }
    auto err = fd.error().value();
    if (!err.isOk()) {
        co_return Unexpected(err);
    }
    co_return Result<>();
}
inline auto QIoContext::accept(SocketView fd) -> Task<std::pair<Socket, IPEndpoint> > {
    while (true) {
        auto ret = fd.accept<Socket>();
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd, PollEvent::In);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto QIoContext::sendto(SocketView fd, const void *buffer, size_t n, const IPEndpoint &endpoint) -> Task<size_t> {
    while (true) {
        auto ret = fd.sendto(buffer, n, 0, endpoint);
        if (ret) {
            co_return ret;
        }
        if (ret.error() != Error::WouldBlock) {
            co_return ret;
        }
        auto pollret = co_await poll(fd, PollEvent::Out);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}
inline auto QIoContext::recvfrom(SocketView fd, void *buffer, size_t n) -> Task<std::pair<size_t, IPEndpoint> > {
    while (true) {
        IPEndpoint endpoint;
        auto ret = fd.recvfrom(buffer, n, 0, &endpoint);
        if (ret) {
            co_return std::make_pair(*ret, endpoint);
        }
        if (ret.error() != Error::WouldBlock) {
            co_return Unexpected(ret.error());
        }
        auto pollret = co_await poll(fd, PollEvent::In);
        if (!pollret) {
            co_return Unexpected(pollret.error());
        }
    }
}

// Poll
inline QIoContext::Notifier::Notifier(QObject *parent, qintptr fd) :
    QObject(parent),
    read(fd, QSocketNotifier::Read),
    write(fd, QSocketNotifier::Write),
    exception(fd, QSocketNotifier::Exception)
{
    setObjectName(QString("Ilias SockNotifier %1").arg(fd));
    read.setEnabled(false);
    write.setEnabled(false);
    exception.setEnabled(false);
}
inline QIoContext::Notifier::~Notifier() {
    Q_EMIT destroyed();
}

inline auto QIoContext::poll(SocketView fd, uint32_t events) -> Task<uint32_t> {
    struct Awaiter {
        Awaiter(qintptr fd, uint32_t events, Notifier *notifier) : fd(fd), notifier(notifier) {
            auto onActive = [this](QSocketDescriptor, QSocketNotifier::Type type) {
                if (type == QSocketNotifier::Read) {
                    revents = PollEvent::In;
                }
                if (type == QSocketNotifier::Write) {
                    revents = PollEvent::Out;
                }
                hasValue = true;
                handle.resume();
            };
            auto onDestroy = [this]() {
                handle.resume();
            };
            if (events & PollEvent::In) {
                inCon = QObject::connect(&notifier->read, &QSocketNotifier::activated, onActive);
                notifier->read.setEnabled(true);
                notifier->numOfRead += 1;
            }
            if (events & PollEvent::Out) {
                outCon = QObject::connect(&notifier->write, &QSocketNotifier::activated, onActive);
                notifier->write.setEnabled(true);
                notifier->numOfWrite += 1;
            }
            destroyCon = QObject::connect(notifier, &QObject::destroyed, onDestroy);
        }
        Awaiter(const Awaiter &) = delete;
        Awaiter(Awaiter &&) = delete;
        ~Awaiter() {
            if (inCon) {
                notifier->read.disconnect(inCon);
                notifier->numOfRead -= 1;
            }
            if (outCon) {
                notifier->write.disconnect(outCon);
                notifier->numOfWrite -= 1;
            }
            // Check all 
            if (notifier->numOfRead == 0) notifier->read.setEnabled(false); 
            if (notifier->numOfWrite == 0) notifier->write.setEnabled(false);
            if (destroyCon) notifier->disconnect(destroyCon); 
        }

        auto await_ready() -> bool { return false; }
        auto await_suspend(Task<uint32_t>::handle_type h) -> void { handle = h; }
        auto await_resume() -> Result<uint32_t> {
            if (!hasValue) {
                return Unexpected(Error::Canceled);
            }
            return revents;
        }

        qintptr fd = qintptr(-1);
        uint32_t revents = 0; //< Receive events
        Notifier *notifier = nullptr;
        bool hasValue = false; //< Does we recived value?
        QMetaObject::Connection inCon;  //< User request in
        QMetaObject::Connection outCon; //< User request out
        QMetaObject::Connection destroyCon; //< The socket was destroyed con
        Task<uint32_t>::handle_type handle; //< Self coroutine handle
    };
    Awaiter awaiter {
        qintptr(fd.get()),
        events,
        mFds[fd.get()]
    };
    co_return co_await awaiter;
}


#if 0
// Extensions

template <typename ...Args>
class QWaitSignal final {
public:
    using Tuple = std::conditional_t<
        (sizeof ...(Args) == 0),
        std::tuple<std::monostate>,
        std::tuple<Args...>
    >;
    using ReturnArgs = std::conditional_t< //< Pack the return args
        (sizeof ...(Args) == 1),
        std::tuple_element_t<0, Tuple>,
        Tuple
    >;
    
    // If object is from an another thread, will it resume us at the object's thread?
    QWaitSignal(auto *object, auto signal) {
        auto onEmit = [this](Args ...args) {
            mResult = ReturnArgs(args...);
            mHandle.resume();
        };
        auto onDestroy = [this]() {
            mHandle.resume();
        };

        // Connect to the user connections
        mCon = QObject::connect(object, signal, onEmit);

        // Check the user is try to waiting destroyed signal
        if constexpr (std::is_same_v<decltype(signal), decltype(&QObject::destroyed)>) {
            if (signal == &QObject::destroyed) return; 
        }
        // We should also resume at the object destroy
        mDestroyCon = QObject::connect(object, &QObject::destroyed, onDestroy);
    }
    QWaitSignal(const QWaitSignal &) = delete;
    QWaitSignal(QWaitSignal &&) = delete;
    ~QWaitSignal() {
        if (mCon) QObject::disconnect(mCon); 
        if (mDestroyCon) QObject::disconnect(mDestroyCon);
    }

    auto await_ready() -> bool { return !mCon || mResult.has_value(); } //< If signal we not connected, we can not wait
    auto await_suspend(std::coroutine_handle<> h) -> void { mHandle = h; }
    auto await_resume() -> std::optional<ReturnArgs> { return std::move(mResult); }
private:
    QMetaObject::Connection mCon;
    QMetaObject::Connection mDestroyCon; //< The connection wating for destroy signal
    std::coroutine_handle<> mHandle;
    std::optional<ReturnArgs> mResult; //< The result values
};

template <typename InClass, typename Ret, typename Class, typename... Args>
QWaitSignal(InClass*, Ret (Class::*fn)(Args...)) -> QWaitSignal<Args...>;
template <typename InClass, typename Ret, typename Class, typename... Args>
QWaitSignal(InClass*, Ret (Class::*fn)(Args...) const) -> QWaitSignal<Args...>;

#endif

ILIAS_NS_END