/**
 * @file object.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides utility functions for QObject. use coroutine to await signals
 * @version 0.1
 * @date 2024-10-13
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/task/task.hpp>
#include <functional>
#include <QObject>

namespace ilias_qt {

namespace runtime = ilias::runtime;

struct DeleteLater {
    auto operator()(QObject *object) const {
        object->deleteLater();
    }
};

template <typename T>
using Box = std::unique_ptr<T, DeleteLater>;

/**
 * @brief Qt signal awaiter
 * @example co_await QSignal(object, &Class::signal);
 * 
 * @tparam Args The arguments of the signal
 */
template <typename ...Args>
class QSignal {
public:
    using Tuple = std::tuple<Args...>;

    /**
     * @brief Construct a new QSignal object, with the same argument as the signal
     * 
     * @param object The object that emit the signal
     * @param signal The signal function
     * @param type The connection type
     */
    template <typename Class, typename RetT, typename SignalClass>
    QSignal(Class *object, RetT (SignalClass::*signal)(Args...)) {
        mFn = [this, object, signal]() {
            doConnect(object, signal);
        };
    }

    /**
     * @brief Construct a new QSignal object, with the different argument as the signal
     * 
     * @param object The object that emit the signal
     * @param signal The signal function
     * @param type The connection type
     */
    template <typename Class, typename Signal>
    QSignal(Class *object, Signal signal) {
        mFn = [this, object, signal]() {
            doConnect(object, signal);
        };
    }

    QSignal(const QSignal &) = delete;
    QSignal(QSignal &&) = default;

    ~QSignal() {
        doDisconnect();
    }

    auto await_ready() -> bool { return false; }
    
    auto await_suspend(runtime::CoroHandle caller) { 
        mFn(); // Do connect the signal
        mFn = nullptr;
        mCaller = caller;
        mReg.register_<&QSignal::onStopRequested>(caller.stopToken(), this);
    }
    
    /**
     * @brief Return the result of the signal (nullopt on object destroyed)
     * 
     * @return std::optional<Tuple> 
     */
    auto await_resume() -> std::optional<Tuple> { return std::move(mResult); }
private:
    auto doConnect(auto object, auto signal) -> void {
        auto onEmit = [this](Args ...args) {
            doDisconnect();
            mResult.emplace(Tuple{std::forward<Args>(args)...});
            mCaller.resume();
        };
        auto onDestroy = [this]() {
            doDisconnect();
            mCaller.schedule();
        };

        mCon = QObject::connect(object, signal, onEmit);

        // Check the user is try to waiting destroyed signal
        if constexpr (std::is_same_v<decltype(signal), decltype(&QObject::destroyed)>) {
            if (signal == &QObject::destroyed) return; 
        }
        // We should also resume at the object destroy
        mDestroyCon = QObject::connect(object, &QObject::destroyed, onDestroy);
    }
    
    auto doDisconnect() -> void {
        if (mCon) {
            QObject::disconnect(mCon);
        }
        if (mDestroyCon) {
            QObject::disconnect(mDestroyCon);
        }
    }

    auto onStopRequested() -> void {
        doDisconnect();
        mCaller.setStopped();
    }

    // Connect function
    std::function<void()> mFn;

    // Status Block
    QMetaObject::Connection mCon;
    QMetaObject::Connection mDestroyCon;
    std::optional<Tuple> mResult;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
};

template <typename T>
class QAsyncSlot {
public:
    QAsyncSlot() = default;
    QAsyncSlot(const QAsyncSlot &) = delete;
    QAsyncSlot(QAsyncSlot &&) = default;
    QAsyncSlot(ilias::Task<T> task) : mHandle(ilias::spawn(std::move(task))) {}

    using promise_type = typename ilias::Task<T>::promise_type;
private:
    ilias::WaitHandle<T> mHandle;
};

} // namespace ilias_qt