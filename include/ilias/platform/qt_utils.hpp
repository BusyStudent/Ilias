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

#include <ilias/detail/functional.hpp>
#include <ilias/task/spawn.hpp>
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
        mFn = [this, object, signal, type]() {
            doConnect(object, signal, type);
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
    QSignal(Class *object, Signal signal, Qt::ConnectionType type = Qt::AutoConnection) {
        mFn = [this, object, signal, type]() {
            doConnect(object, signal, type);
        };
    }

    QSignal(const QSignal &) = delete;
    QSignal(QSignal &&) = default;

    ~QSignal() {
        doDisconnect();
    }

    auto await_ready() -> bool { return false; }
    
    auto await_suspend(TaskView<> caller) { 
        mFn(); // Do connect the signal
        mFn = nullptr;
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
    
    auto doDisconnect() -> void {
        if (mCon) {
            QObject::disconnect(mCon);
        }
        if (mDestroyCon) {
            QObject::disconnect(mDestroyCon);
        }
    }

    static auto onCancel(void *_self) -> void {
        auto self = reinterpret_cast<QSignal *>(_self);
        self->doDisconnect();
        self->mCaller.schedule();
    }

    // Connect function
    detail::MoveOnlyFunction<void()> mFn;

    // Status Block
    QMetaObject::Connection mCon;
    QMetaObject::Connection mDestroyCon;
    TaskView<>              mCaller;
    std::optional<ReturnType> mResult;
    CancellationToken::Registration mReg;
};

/**
 * @brief The Slot with coroutine support, it will execute immediately on the slot was invoke
 * @note The argument should not be reference, it will be de danglinged when coroutine was suspended
 * 
 * @code
 *  class MyQClass : public QObject {
 *  public:
 *      auto onButtonClicked() -> QAsyncSlot<void> {
 *          co_await xxx;
 *          co_return;
 *      }
 *  }
 * @endcode
 * @tparam T 
 */
template <typename T = void>
class QAsyncSlot {
public:
    using promise_type = typename Task<T>::promise_type;

    QAsyncSlot(Task<T> task) : mHandle(spawnImmediate(std::move(task))) { }
    QAsyncSlot(QAsyncSlot &&) = default;
    QAsyncSlot() = default;

    auto operator =(QAsyncSlot &&other) -> QAsyncSlot & {
        mHandle = std::move(other.mHandle);
        return *this;
    }

    auto operator co_await() && noexcept {
        return std::move(mHandle).operator co_await();
    }
private:
    WaitHandle<T> mHandle;
};


ILIAS_NS_END