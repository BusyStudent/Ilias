/**
 * @file qt_utils.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides utility functions for Qt. use coroutine to await signals
 * @version 0.1
 * @date 2024-10-13
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/task/task.hpp>
#include <QMetaObject>
#include <QObject>
#include <optional>
#include <variant> //< For std::monostate
#include <tuple>

ILIAS_NS_BEGIN

/**
 * @brief Qt signal awaiter
 * @example co_await QSignal(object, &Class::signal);
 * 
 * @tparam Args The arguments of the signal
 */
template <typename ...Args>
class QSignal {
public:
    using Tuple = std::conditional_t<
        (sizeof ...(Args) == 0),
        std::tuple<std::monostate>,
        std::tuple<Args...>
    >;
    using ReturnType = std::conditional_t<
        (sizeof...(Args) == 1),
        std::tuple_element_t<0, Tuple>,
        Tuple
    >;

    /**
     * @brief Construct a new QSignal object, with the same argument as the signal
     * 
     * @param object The object that emit the signal
     * @param signal The signal function
     * @param type The connection type
     */
    template <typename Class, typename RetT, typename SignalClass>
    QSignal(Class *object, RetT (SignalClass::*signal)(Args...), Qt::ConnectionType type = Qt::AutoConnection) {
        doConnect(object, signal, type);
    }

    /**
     * @brief Construct a new QSignal object, with the different argument as the signal
     * 
     * @param object The object that emit the signal
     * @param signal The signal function
     * @param type The connection type
     */
    template <typename Class, typename Signal>
    QSignal(Class *object, Signal signal, Qt::ConnectionType type = Qt::AutoConnection) {
        doConnect(object, signal, type);
    }

    QSignal(const QSignal &) = delete;

    ~QSignal() {
        doDisconnect();
    }

    auto await_ready() -> bool { return false; }
    
    auto await_suspend(TaskView<> caller) { 
        mCaller = caller; 
        mReg = mCaller.cancellationToken().register_(onCancel, this); 
    }
    
    auto await_resume() -> std::optional<ReturnType> { return std::move(mResult); }
private:
    auto doConnect(auto object, auto signal, Qt::ConnectionType type) -> void {
        auto onEmit = [this](Args ...args) {
            doDisconnect();
            mResult.emplace(ReturnType{std::forward<Args>(args)...});
            mCaller.resume();
        };
        auto onDestroy = [this]() {
            doDisconnect();
            mCaller.schedule();
        };

        mCon = QObject::connect(object, signal, onEmit, type);

        // Check the user is try to waiting destroyed signal
        if constexpr (std::is_same_v<decltype(signal), decltype(&QObject::destroyed)>) {
            if (signal == &QObject::destroyed) return; 
        }
        // We should also resume at the object destroy
        mDestroyCon = QObject::connect(object, &QObject::destroyed, onDestroy, type);
    }
    
    auto doDisconnect() {
        if (mCon) {
            QObject::disconnect(mCon);
        }
        if (mDestroyCon) {
            QObject::disconnect(mDestroyCon);
        }
    }

    static auto onCancel(void *+self) -> void {
        auto self = reinterpret_cast<QSignal *>(self);
        self->doDisconnect();
        self->mCaller.schedule();
    }

    // Status Block
    QMetaObject::Connection mCon;
    QMetaObject::Connection mDestroyCon;
    TaskView<>              mCaller;
    std::optional<ReturnType> mResult;
    CancellationToken::Registration mReg;
};

ILIAS_NS_END