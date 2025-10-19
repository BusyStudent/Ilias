/**
 * @file mpsc.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The multi producer single consumer channel.
 * @version 0.1
 * @date 2024-10-30
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/sync/detail/futex.hpp> // FutexMutex
#include <ilias/sync/detail/queue.hpp> // WaitQueue
#include <ilias/task/task.hpp>
#include <ilias/result.hpp> // Result
#include <concepts>
#include <memory> // std::unique_ptr, std::shared_ptr
#include <limits>
#include <atomic> // std::atomic
#include <deque>

ILIAS_NS_BEGIN

namespace mpsc {
namespace detail {

template <typename T>
class Channel final {
public:
    Channel(size_t c) : capacity(c) { }
    Channel(Channel &&) = delete;
    ~Channel() { // Check state
        ILIAS_ASSERT(receiverClosed);
        ILIAS_ASSERT(senderClosed);
    }

    // For sender
    auto trySendInternal(Result<void, T> &item) -> bool {
        auto locker = std::lock_guard(mutex);
        if (receiverClosed) { // We can't send any more data.
            return true;
        }
        if (queue.size() >= capacity) {
            return false; // No space, continue to wait.
        }
        queue.emplace_back(std::move(item.error()));
        item = {};
        return true; // Sended
    }

    // For receiver
    auto tryRecvInternal(std::optional<T> &value) -> bool {
        auto locker = std::lock_guard(mutex);
        if (!queue.empty()) {
            value.emplace(std::move(queue.front()));
            queue.pop_front();
            return true; // Have data,
        }
        if (senderClosed) {
            return true; // No sender, so we can't receive any more.
        }
        return false;
    }

    auto deref() -> void {
        auto prev = refcount.fetch_sub(1, std::memory_order_acq_rel);
        ILIAS_ASSERT_MSG(prev != 0, "Can't deref a channel that is already destroyed");
        if (prev == 1) { // Last one
            delete this;
        }
    }

    // States
    const size_t          capacity       {0};     // The capacity of the channel. read only.
    bool                  senderClosed   {false}; // If all the sender is closed.
    bool                  receiverClosed {false};
    std::atomic<uint8_t>  refcount       {2};     // for sender & receiver, on 0 destroy self

    // Sync, all queue's wakeup must call without lock the mutex, we may lock the mutex in the onWakeup. it will deadlock.
    sync::FutexMutex      mutex; // Protect the queue, senderCount, and receiverClosed.
    sync::WaitQueue       senders;
    sync::WaitQueue       receiver;
    std::deque<T>         queue; // For storing the items.
};

class ChanSenderDeleter final {
public:
    template <typename T>
    auto operator ()(Channel<T> *chan) {
        bool notify = false;
        {
            auto locker = std::lock_guard(chan->mutex);
            chan->senderClosed = true; // All sender is closed.
            notify = !chan->receiverClosed; // If the receiver is closed, not need to notify.
        }
        if (notify) { // Receiver is not closed, we should wakeup the receiver.
            chan->receiver.wakeupOne();
        }
        chan->deref();
    }
};

class ChanReceiverDeleter final {
public:
    template <typename T>
    auto operator ()(Channel<T> *chan) {
        bool notify = false;
        {
            auto locker = std::lock_guard(chan->mutex);
            chan->receiverClosed = true; // All receiver is closed.
            notify = !chan->senderClosed; // If the sender is all closed, not need to notify.
        }
        if (notify) {
            chan->senders.wakeupAll(); // Because it is multi producer, use wakeupAll
        }
        chan->deref();
    }
};

template <typename T>
class SendAwaiter final : public sync::WaitAwaiter<SendAwaiter<T> > {
public:
    SendAwaiter(Channel<T> *c, T value) : sync::WaitAwaiter<SendAwaiter<T> >(c->senders), mChan(c), mResult(Err(std::move(value))) {};
    SendAwaiter(SendAwaiter &&) = default;

    auto await_resume() -> Result<void, T> {
        if (mResult) { // Is sended
            mChan->receiver.wakeupOne();
        }
        return std::move(mResult);
    }

    auto onWakeup() -> bool {
        return mChan->trySendInternal(mResult);
    }
private:
    Channel<T> *mChan;
    Result<void, T> mResult;
};

template <typename T>
class ReceiveAwaiter final : public sync::WaitAwaiter<ReceiveAwaiter<T> > {
public:
    ReceiveAwaiter(Channel<T> *c) : sync::WaitAwaiter<ReceiveAwaiter<T> >(c->receiver), mChan(c) { }
    ReceiveAwaiter(ReceiveAwaiter &&) = default;

    auto await_resume() -> std::optional<T> {
        if (mValue) {
            mChan->senders.wakeupOne();
        }
        return std::move(mValue);
    }

    auto onWakeup() -> bool {
        return mChan->tryRecvInternal(mValue);
    }
private:
    Channel<T>      *mChan;
    std::optional<T> mValue; // The value we got
};

template <typename T>
using ChanSender   = std::shared_ptr<Channel<T> >;

template <typename T>
using ChanReceiver = std::unique_ptr<Channel<T>, ChanReceiverDeleter>;

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

/**
 * @brief The mpsc sender class. This class is used to send data to the channel. (copy & move able)
 * 
 * @tparam T The item type to be sent.
 */
template <Sendable T>
class Sender final {
public:
    Sender(const Sender &) = default;
    Sender(Sender &&) = default;
    Sender() = default;
    ~Sender() = default;

    /**
     * @brief Close the sender
     * 
     */
    auto close() noexcept -> void {
        mChan.reset();
    }

    /**
     * @brief Check if the channel is closed. we can't send any more data
     * 
     * @note The return value is only stable on single thread use.
     * 
     * @return true 
     * @return false 
     */
    auto isClosed() const noexcept -> bool {
        if (!mChan) {
            return true;
        }
        auto locker = std::lock_guard(mChan->mutex);
        return mChan->receiverClosed;
    }

    /**
     * @brief Get the capacity of the channel.
     * 
     * @return size_t 
     */
    auto capacity() const noexcept -> size_t {
        return mChan ? mChan->capacity : 0;
    }

    /**
     * @brief Send a item to the channel.
     * 
     * @param item The item to be sent.
     * @return Result<void, T>, if the receiver is closed, return Err(item), else return {}.
     */
    [[nodiscard]]
    auto send(T item) const noexcept {
        return detail::SendAwaiter<T>(mChan.get(), std::move(item));
    }

    /**
     * @brief Blocking send a item to the channel.
     * 
     * @param item 
     * @note It will ```BLOCK``` the thread, so it is not recommended to use it in the async context, use it in sync code
     * @return Result<void, T>, if the receiver is closed, return Err(item), else return {}.
     */
    [[nodiscard]]
    auto blockingSend(T item) const -> Result<void, T> {
        auto result = Result<void, T>(Err(std::move(item))); // First put it to error as unsended.
        mChan->senders.blockingWait([&]() { return mChan->trySendInternal(result); });
        if (result) { // Success to send, wakeup the receiver.
            mChan->receiver.wakeupOne();
        }
        return result;
    }

    auto operator =(Sender &&other) -> Sender & = default;
    auto operator =(const Sender &other) -> Sender & = default;

    // Check the sender is valid
    explicit operator bool() const noexcept {
        return bool(mChan);
    }
private:
    explicit Sender(detail::ChanSender<T> chan) : mChan(std::move(chan)) {}
    detail::ChanSender<T> mChan;
template <Sendable U>
friend auto channel(size_t capacity) -> Pair<U>;
};

/**
 * @brief The mpsc receiver class. This class is used to receive data from the channel. (only moveable)
 * 
 * @tparam T The item type to be received.
 */
template <Sendable T>
class Receiver final {
public:
    Receiver() = default;
    Receiver(const Receiver &) = delete;
    Receiver(Receiver &&) = default;
    ~Receiver() = default;

    auto close() noexcept -> void {
        mChan.reset();
    }

    /**
     * @brief Check if the channel is closed. no sender can send any more data
     * 
     * @note The return value is only stable on single thread use.
     * 
     * @return true 
     * @return false 
     */
    auto isClosed() const noexcept -> bool {
        if (!mChan) {
            return true;
        }
        auto locker = std::lock_guard(mChan->mutex);
        return mChan->senderClosed;
    }

    /**
     * @brief Receive a item from the channel.
     * @note Don't use the recv method concurrently, only one task can use it at a time.
     * 
     * @return std::optional<T>, nullopt on the channel is closed
     */
    [[nodiscard]]
    auto recv() noexcept {
        return detail::ReceiveAwaiter<T>(mChan.get());
    }

    /**
     * @brief Blocking Receive a item from the channel.
     * @note 
     *  - Don't use the recv method concurrently, only one thread can use it at a time.
     * 
     *  - It will ```BLOCK``` the thread, so it is not recommended to use it in the async context, use it in sync code
     * 
     * @return std::optional<T>, nullopt on the channel is closed
     */
    [[nodiscard]]
    auto blockingRecv() noexcept(std::is_nothrow_move_constructible_v<T>) -> std::optional<T> {
        auto value = std::optional<T>();
        mChan->receiver.blockingWait([&]() { return mChan->tryRecvInternal(value); });
        if (value) { // Success to recv, wakeup the sender.
            mChan->senders.wakeupOne();
        }
        return value;
    }

    auto operator =(const Receiver &other) = delete;
    auto operator =(Receiver &&other) -> Receiver & = default;

    // Check the receiver is valid
    explicit operator bool() const noexcept {
        return bool(mChan);   
    }
private:
    explicit Receiver(detail::Channel<T> *chan) : mChan(chan) {}

    detail::ChanReceiver<T> mChan;
template <Sendable U>
friend auto channel(size_t capacity) -> Pair<U>;
};

/**
 * @brief Make a channel for multi producer and single consumer.
 * 
 * @tparam T The item type to be sent and received. (must be moveable)
 * @param capacity The capacity of the channel. (abort on 0), default on SIZET_MAX (no limit)
 * @return Pair<T> 
 */
template <Sendable T>
inline auto channel(size_t capacity = std::numeric_limits<size_t>::max()) -> Pair<T> {
    ILIAS_ASSERT_MSG(capacity > 0, "The capacity of the channel must be greater than 0.");
    auto ptr = new detail::Channel<T>(capacity);
    return {
        .sender = Sender<T> {
            detail::ChanSender<T> {
                ptr, detail::ChanSenderDeleter {}
            }
        },
        .receiver = Receiver<T> {ptr}
    };
}

} // namespace mpsc

ILIAS_NS_END