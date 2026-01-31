/**
 * @file oneshot.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Oneshot channel implementation
 * @version 0.1
 * @date 2024-08-31
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/sync/detail/futex.hpp> // FutexMutex
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <ilias/result.hpp>
#include <ilias/log.hpp>
#include <optional>
#include <concepts>
#include <memory> // std::unique_ptr
#include <atomic> // std::atomic
#include <mutex> // std::lock_guard

ILIAS_NS_BEGIN

/**
 * @brief Channel for sending one value, the sender and receiver are not copyable, only movable, it is thread-safe
 * 
 */
namespace oneshot {
namespace detail {

/**
 * @brief The data block for the channel, the reciever take the ownership of the data block
 * 
 * @tparam T 
 */
template <typename T>
class Channel {
public:
    auto notify() -> void {
        if (receiver) {
            receiver.schedule();
            receiver = nullptr;
        }
        finally.store(true);
        finally.notify_one();
    }

    auto lock() {
        mutex.lock();
    }

    auto unlock() {
        mutex.unlock();
    }

    runtime::CoroHandle   receiver; // The caller that is suspended on the recv operation
    std::optional<T>      value;
    sync::FutexMutex      mutex;

    // States, TODO: Compress the state to a single byte?
    std::atomic<bool>     finally       {false}; // For blocking recv, set true when sender close or value is set
    bool                  valueGot      {false}; // Did the receiver get the value ?(alreay got)
    bool                  senderClose   {false};
    bool                  receiverClose {false};
};

class ChanSenderDeleter {
public:
    template <typename T>
    auto operator()(Channel<T> *chan) -> void {
        bool delete_ = false;
        {
            auto locker = std::lock_guard(*chan);
            chan->senderClose = true;
            chan->notify(); // Notify the receiver we are closed
            delete_ = chan->receiverClose; // Reciever already closed, no-one own the data block, delete!
        }

        if (delete_) {
            delete chan;
        }
    }
};

class ChanReceiverDeleter {
public:
    template <typename T>
    auto operator()(Channel<T> *chan) -> void {
        bool delete_ = false;
        {
            auto locker = std::lock_guard(*chan);
            chan->receiverClose = true;
            delete_ = chan->senderClose; // Sender already closed, no-one own the data block, delete!
        }

        if (delete_) {
            delete chan;
        }
    }
};

template <typename T>
using ChanSender = std::unique_ptr<Channel<T>, ChanSenderDeleter>;

template <typename T>
using ChanReceiver = std::unique_ptr<Channel<T>, ChanReceiverDeleter>;

/**
 * @brief Do the recv operation
 * 
 * @tparam T 
 */
template <typename T>
class RecvAwaiter {
public:
    RecvAwaiter(ChanReceiver<T> chan) : mChan(std::move(chan)), mLocker(*mChan) { } // GOT ownship and lock here
    RecvAwaiter(RecvAwaiter &&other) = default;
    ~RecvAwaiter() = default;

    auto await_ready() const -> bool { 
        // Already sended or broken
        return mChan->value.has_value() || mChan->senderClose;
    }

    auto await_suspend(runtime::CoroHandle caller) -> void {
        mChan->receiver = caller;
        mLocker.unlock(); // Unlock the channel, lock again when resume or stopRequested
        mReg.register_<&RecvAwaiter::onStopRequested>(caller.stopToken(), this);
    }

    auto await_resume() -> std::optional<T> {
        if (!mLocker.owns_lock()) {
            mLocker.lock();
        }
        ILIAS_ASSERT(!mChan->valueGot, "Double call on recv ?, value already got");
        ILIAS_ASSERT(!mChan->receiver);

        if (mChan->value) { // Value is set
            mChan->valueGot = true;
            return std::move(*mChan->value);
        }
        ILIAS_ASSERT(mChan->senderClose); // Should be closed, if not, wrong state
        return std::nullopt;
    }
private:
    auto onStopRequested() -> void { // On the executor thread
        mLocker.lock();
        auto handle = std::exchange(mChan->receiver, nullptr);
        mLocker.unlock();

        if (handle) { // Not already scheduled
            handle.setStopped();
        }
    }

    ChanReceiver<T>               mChan;
    std::unique_lock<Channel<T> > mLocker; // Lock the channel, unlock before destroy the chan
    runtime::StopRegistration     mReg;
};

} // namespace detail


template <typename T>
concept Sendable = std::movable<T> && (!std::is_reference_v<T>);

template <Sendable T>
class Sender;

template <Sendable T>
class Receiver;

template <Sendable T>
struct Pair {
    Sender<T>   sender;
    Receiver<T> receiver;
};

enum class TryRecvError {
    Empty,
    Closed
};

/**
 * @brief The receiver of the channel (move only)
 * 
 * @tparam T 
 */
template <Sendable T>
class Receiver {
public:
    Receiver() = default;
    Receiver(std::nullptr_t) {}
    Receiver(const Receiver &) = delete;
    Receiver(Receiver &&other) = default;

    // Close the receiver
    auto close() noexcept -> void {
        return mChan.reset();
    }

    [[nodiscard]]
    auto empty() const -> bool {
        return isEmpty();
    }

    [[nodiscard]]
    auto isClosed() const -> bool {
        if (!mChan) {
            return true;
        }
        auto locker = std::lock_guard(*mChan);
        return mChan->senderClose;
    }

    [[nodiscard]]
    auto isEmpty() const -> bool {
        if (!mChan) {
            return true;
        }
        auto locker = std::lock_guard(*mChan);
        return !mChan->value.has_value();
    }

    /**
     * @brief Get the value from the channel, it will `CONSUME` the receiver
     * 
     * @return std::optional<T>, nullopt on closed
     */
    [[nodiscard]]
    auto recv() {
        return detail::RecvAwaiter<T>(std::move(mChan));
    }

    /**
     * @brief Try to recv the value from the channel, it will `CONSUME` the receiver if success
     * 
     * @return Result<T, TryRecvError> 
     */
    [[nodiscard]]
    auto tryRecv() -> Result<T, TryRecvError> {
        if (isClosed()) {
            return Err(TryRecvError::Closed);
        }
        auto locker = std::unique_lock(*mChan);

        // Check value
        if (!mChan->value) {
            return Err(TryRecvError::Empty);
        }

        // Take the value
        auto value = std::move(*mChan->value);
        mChan->valueGot = true;

        // Then, consume the receiver
        locker.unlock();
        mChan.reset();
        return std::move(value);
    }

    /**
     * @brief Block until the value is received or the channel is closed
     * @note It will ```BLOCK``` the thread, so it is not recommended to use it in the async context, use it in sync code
     * 
     * @return std::optional<T> nullopt on closed
     */
    [[nodiscard]]
    auto blockingRecv() -> std::optional<T> {
        mChan->finally.wait(false); // Wait the result
        auto locker = std::lock_guard(*mChan);

        if (mChan->value) {
            mChan->valueGot = true;
            return std::move(*mChan->value);
        }
        // Channel is closed
        ILIAS_ASSERT(mChan->senderClose);
        return std::nullopt;
    }

    auto operator =(Receiver &&) -> Receiver & = default;

    /**
     * @brief Awaits the value from the channel
     * 
     * @return std::optional<T>, nullopt on closed 
     */
    auto operator co_await() && -> detail::RecvAwaiter<T> {
        return detail::RecvAwaiter<T>(std::move(mChan));
    }

    /**
     * @brief Check if the receiver is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mChan);
    }
private:
    explicit Receiver(detail::Channel<T> *ptr) : mChan(ptr) { }

    detail::ChanReceiver<T> mChan;
template <Sendable U>
friend auto channel() -> Pair<U>;
};

/**
 * 
 * @brief The sender of the channel (move only)
 * 
 * @tparam T 
 */
template <Sendable T>
class Sender {
public:
    Sender() = default;
    Sender(std::nullptr_t) { }
    Sender(Sender &&other) = default;
    ~Sender() = default;

    auto close() -> void {
        mChan.reset();
    }

    // Check the sender is closed, it is safe to call on an empty sender (empty as closed)
    [[nodiscard]]
    auto isClosed() -> bool {
        if (!mChan) {
            return true;
        }
        auto locker = std::lock_guard(*mChan);
        return mChan->senderClose;
    }

    /**
     * @brief Send the value to the channel, if closed or already send, return the value as error
     * 
     * @param value 
     * @return Result<void, T> 
     */
    [[nodiscard]]
    auto send(T value) -> Result<void, T> {
        if (!mChan) {
            return Err(std::move(value));
        }

        auto locker = std::lock_guard(*mChan);
        if (mChan->value) { // already send
            return Err(std::move(value));
        }
        if (mChan->receiverClose) { // already close
            return Err(std::move(value));
        }
        mChan->value.emplace(std::move(value));
        mChan->notify();
        return {};
    }

    auto operator =(const Sender &) = delete;
    auto operator =(Sender &&other) -> Sender & = default;
    auto operator <=>(const Sender &) const = default;
private:
    explicit Sender(detail::Channel<T> *ptr) : mChan(ptr) {}

    detail::ChanSender<T> mChan;
template <Sendable U>
friend auto channel() -> Pair<U>;
};

/**
 * @brief Make a channel for the oneshot communication
 * 
 * @tparam T require Sendable
 * @return Pair<T> 
 */
template <Sendable T>
inline auto channel() -> Pair<T> {
    auto ptr = new detail::Channel<T>();
    return {
        .sender = Sender<T>(ptr),
        .receiver = Receiver<T>(ptr)
    };
}

} // namespace oneshot

ILIAS_NS_END