/**
 * @file mpmc.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The multi producer multi consumer channel.
 * @version 0.1
 * @date 2026-07-16
 * 
 * @copyright Copyright (c) 2026
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

// TODO: Duplicated code with mpmc.hpp, maye we can refactor it?

ILIAS_NS_BEGIN

namespace mpmc {

// Forward declaration
template <typename T>
concept Sendable = std::is_move_constructible_v<T> && (!std::is_reference_v<T>);

template <Sendable T>
class Permit;

// Implementation
namespace detail {

template <Sendable T>
class Channel final {
public:
    Channel(size_t c) : capacity(c) {}
    Channel(Channel &&) = delete;
    ~Channel() { // Check state
        ILIAS_ASSERT(receiverClosed);
        ILIAS_ASSERT(senderClosed);
    }

    // For sender
    auto trySendInternal(Result<void, T> &item) -> bool {
        std::lock_guard locker {mutex};
        if (receiverClosed) { // We can't send any more data.
            return true;
        }
        if (queue.size() + reserved >= capacity) {
            return false; // No space, continue to wait.
        }
        queue.emplace_back(std::move(item.error()));
        item = {};
        return true; // Sended
    }

    // Returns true when wait is finished. outGot is true only when a slot was reserved.
    auto tryReserveInternal(bool &outGot) -> bool {
        std::lock_guard locker {mutex};
        if (receiverClosed) { // We can't send any more data.
            outGot = false;
            return true;
        }
        if (queue.size() + reserved >= capacity) {
            return false; // No space, continue to wait.
        }
        ++reserved;
        outGot = true;
        return true; // Reserved
    }

    // For receiver
    auto tryRecvInternal(std::optional<T> &value) -> bool {
        std::lock_guard locker {mutex};
        if (!queue.empty()) {
            value.emplace(std::move(queue.front()));
            queue.pop_front();
            return true; // Have data
        }
        if (senderClosed) {
            return true; // No sender, so we can't receive any more.
        }
        return false;
    }

    auto deref() -> void {
        auto prev = refcount.fetch_sub(1, std::memory_order_acq_rel);
        ILIAS_ASSERT(prev != 0, "Can't deref a channel that is already destroyed");
        if (prev == 1) { // Last one
            delete this;
        }
    }

    // States
    const size_t          capacity       {0};     // The capacity of the channel. read only.
    bool                  senderClosed   {false}; // If all the sender is closed.
    bool                  receiverClosed {false}; // If all the receiver is closed.
    std::atomic<uint8_t>  refcount       {2};     // for sender & receiver sides, on 0 destroy self

    // Sync, all queue's wakeup must call without lock the mutex, we may lock the mutex in the onWakeup. it will deadlock.
    sync::FutexMutex      mutex; // Protect the queue, reserved, and closed flags.
    sync::WaitQueue       senders;
    sync::WaitQueue       receivers;
    std::deque<T>         queue; // For storing the items.
    size_t                reserved {0}; // The num of reserved items
};

class ChanSenderDeleter final {
public:
    template <Sendable T>
    auto operator ()(Channel<T> *chan) {
        bool notify = false;
        {
            std::lock_guard locker {chan->mutex};
            chan->senderClosed = true; // All sender is closed.
            notify = !chan->receiverClosed; // If the receiver is closed, not need to notify.
        }
        if (notify) { // Wake all receivers so they can observe the closed state.
            chan->receivers.wakeupAll();
        }
        chan->deref();
    }
};

class ChanReceiverDeleter final {
public:
    template <Sendable T>
    auto operator ()(Channel<T> *chan) {
        bool notify = false;
        {
            std::lock_guard locker {chan->mutex};
            chan->receiverClosed = true; // All receiver is closed.
            notify = !chan->senderClosed; // If the sender is all closed, not need to notify.
        }
        if (notify) {
            chan->senders.wakeupAll(); // Multi producer, use wakeupAll
        }
        chan->deref();
    }
};

// Used for Permit<T>
class ChanPermitDeleter final {
public:
    template <Sendable T>
    auto operator ()(Channel<T> *chan) { // Give the reserved item slot back to the channel.
        bool notify = false;
        {
            std::lock_guard locker {chan->mutex};
            chan->reserved -= 1; // Release the reserved item
            notify = chan->queue.size() + chan->reserved < chan->capacity; // If we have space, we should wakeup the an sender.
        }
        if (notify) {
            chan->senders.wakeupOne();
        }
    }
};

template <Sendable T>
class SendAwaiter final : public sync::WaitAwaiter<SendAwaiter<T> > {
public:
    SendAwaiter(Channel<T> *c, T value) : sync::WaitAwaiter<SendAwaiter<T> >(c->senders), mChan(c), mResult(Err(std::move(value))) {}
    SendAwaiter(SendAwaiter &&) = default;

    auto await_resume() -> Result<void, T> {
        if (mResult) { // Is sended
            mChan->receivers.wakeupOne();
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

template <Sendable T>
class ReceiveAwaiter final : public sync::WaitAwaiter<ReceiveAwaiter<T> > {
public:
    ReceiveAwaiter(Channel<T> *c) : sync::WaitAwaiter<ReceiveAwaiter<T> >(c->receivers), mChan(c) {}
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

template <Sendable T>
class ReserveAwaiter final : public sync::WaitAwaiter<ReserveAwaiter<T> > {
public:
    ReserveAwaiter(Channel<T> *c) : sync::WaitAwaiter<ReserveAwaiter<T> >(c->senders), mChan(c) {}
    ReserveAwaiter(ReserveAwaiter &&) = default;

    auto await_resume() -> std::optional<Permit<T> > {
        if (!mGot) {
            return std::nullopt;
        }
        return Permit<T> {mChan};
    }

    auto onWakeup() -> bool {
        return mChan->tryReserveInternal(mGot);
    }
private:
    Channel<T> *mChan;
    bool        mGot = false;
};

// Sender side and receiver side own independent shared_ptr control blocks on the same Channel*.
// Last side destruction runs its custom deleter then Channel::deref.
template <Sendable T>
using ChanSender     = std::shared_ptr<Channel<T> >;

template <Sendable T>
using ChanWeakSender = std::weak_ptr<Channel<T> >;

template <Sendable T>
using ChanReceiver   = std::shared_ptr<Channel<T> >;

template <Sendable T>
using ChanWeakReceiver = std::weak_ptr<Channel<T> >;

template <Sendable T>
using ChanPermit     = std::unique_ptr<Channel<T>, ChanPermitDeleter>;

} // namespace detail

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

enum class TrySendError {
    Full,
    Closed
};

template <typename T>
struct TrySendErrorResult {
    T            item;
    TrySendError reason;
};

/**
 * @brief The mpmc permit class, this class is used to reserve a item slot for the channel.
 * 
 * @tparam T 
 */
template <Sendable T>
class Permit final {
public:
    Permit() = default;
    Permit(Permit &&) = default;

    /**
     * @brief Send the item to the channel. it will ```CONSUME``` the permit.
     * 
     * @param item 
     */
    auto send(T item) -> void {
        auto ptr = std::exchange(mChan, nullptr);
        ILIAS_ASSERT(ptr, "Can't send on a invalid permit");
        {
            std::lock_guard locker {ptr->mutex};
            ptr->queue.emplace_back(std::move(item));
            ptr->reserved -= 1;
        }
        ptr->receivers.wakeupOne();
        ptr.release(); // Don't decrease the reserved count, we already do it
    }

    // Give up the permit
    auto close() -> void {
        mChan.reset();
    }

    auto operator =(Permit &&) -> Permit & = default;
    auto operator =(const Permit &) -> Permit & = delete;

    // Check the permit is valid
    explicit operator bool() const noexcept {
        return bool(mChan);
    }
private:
    Permit(detail::Channel<T> *ptr) : mChan(ptr) {}

    detail::ChanPermit<T> mChan;
template <Sendable U>
friend class Sender;
template <Sendable U>
friend class detail::ReserveAwaiter;
};

/**
 * @brief The mpmc sender class. This class is used to send data to the channel. (copy & move able)
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
        std::lock_guard locker {mChan->mutex};
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
     * @note Cancellation: If the send is cancelled, the item will be lost.
     * 
     * @param item The item to be sent.
     * @return Result<void, T>, if the receiver is closed, return Err(item), else return {}.
     */
    [[nodiscard]]
    auto send(T item) const noexcept {
        return detail::SendAwaiter<T> {mChan.get(), std::move(item)};
    }

    /**
     * @brief Try send a item to the channel.
     * 
     * @param item The item to be sent.
     * @return Result<void, TrySendErrorResult<T> >
     */
    [[nodiscard]]
    auto trySend(T item) const -> Result<void, TrySendErrorResult<T> > {
        std::unique_lock locker {mChan->mutex};

        // Do check
        if (mChan->receiverClosed) {
            return Err(TrySendErrorResult<T> { .item = std::move(item), .reason = TrySendError::Closed });
        }
        if (mChan->queue.size() + mChan->reserved >= mChan->capacity) {
            return Err(TrySendErrorResult<T> { .item = std::move(item), .reason = TrySendError::Full });
        }
        mChan->queue.emplace_back(std::move(item));
        locker.unlock();

        // Success to send, wakeup one receiver.
        mChan->receivers.wakeupOne();
        return {};
    }

    /**
     * @brief Blocking send a item to the channel.
     * @note It will ```BLOCK``` the thread, so it is not recommended to use it in the async context, use it in sync code
     * 
     * @param item 
     * @return Result<void, T>, if the receiver is closed, return Err(item), else return {}.
     */
    [[nodiscard]]
    auto blockingSend(T item) const -> Result<void, T> {
        Result<void, T> result {Err(std::move(item))}; // First put it to error as unsended.
        mChan->senders.blockingWait([&]() { return mChan->trySendInternal(result); });
        if (result) { // Success to send, wakeup one receiver.
            mChan->receivers.wakeupOne();
        }
        return result;
    }

    /**
     * @brief Reserve a item slot for the channel.
     * 
     * @return std::optional<Permit<T> >, nullopt on closed channel.
     */
    [[nodiscard]]
    auto reserve() const noexcept {
        return detail::ReserveAwaiter<T> {mChan.get()};
    }

    /**
     * @brief Try reserve a item slot for the channel.
     * 
     * @return Result<Permit<T>, TrySendError> 
     */
    [[nodiscard]]
    auto tryReserve() const -> Result<Permit<T>, TrySendError> {
        std::unique_lock locker {mChan->mutex};

        // Do check
        if (mChan->receiverClosed) {
            return Err(TrySendError::Closed);
        }
        if (mChan->queue.size() + mChan->reserved >= mChan->capacity) {
            return Err(TrySendError::Full);
        }

        mChan->reserved++;
        return Permit<T> {mChan.get()};
    }

    /**
     * @brief Get the refcount of all the senders.
     * 
     * @return size_t 
     */
    [[nodiscard]]
    auto useCount() const noexcept -> size_t {
        return mChan ? mChan.use_count() : 0;
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
template <Sendable U>
friend class WeakSender;
};

/**
 * @brief The weak sender, it doesn't prevent the channel from being closed.
 * 
 * @tparam T The item type to be sent.
 */
template <Sendable T>
class WeakSender final {
public:
    WeakSender() = default;
    WeakSender(const WeakSender &) = default;
    WeakSender(WeakSender &&) = default;
    ~WeakSender() = default;

    auto close() noexcept -> void {
        mChan.reset();
    }

    // Try upgrade the weak to strong
    auto lock() -> Sender<T> {
        return Sender<T> {mChan.lock()};
    }
private:
    detail::ChanWeakSender<T> mChan;
};

/**
 * @brief The mpmc receiver class. This class is used to receive data from the channel. (copy & move able)
 * 
 * @tparam T The item type to be received.
 */
template <Sendable T>
class Receiver final {
public:
    Receiver() = default;
    Receiver(const Receiver &) = default;
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
        std::lock_guard locker {mChan->mutex};
        return mChan->senderClosed;
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
     * @brief Receive a item from the channel.
     * @note Concurrent recv on multiple receivers is supported.
     * 
     * @return std::optional<T>, nullopt on the channel is closed
     */
    [[nodiscard]]
    auto recv() noexcept {
        return detail::ReceiveAwaiter<T> {mChan.get()};
    }

    /**
     * @brief Try receive a item from the channel.
     * 
     * @return Result<T, TryRecvError> 
     */
    [[nodiscard]]
    auto tryRecv() noexcept(std::is_nothrow_move_constructible_v<T>) -> Result<T, TryRecvError> {
        std::unique_lock locker {mChan->mutex};
        if (mChan->queue.empty() && mChan->senderClosed) {
            return Err(TryRecvError::Closed);
        }
        if (mChan->queue.empty()) {
            return Err(TryRecvError::Empty);
        }
        auto value = std::move(mChan->queue.front());
        mChan->queue.pop_front();
        locker.unlock();

        // Success to recv, wakeup one sender.
        mChan->senders.wakeupOne();
        return std::move(value);
    }

    /**
     * @brief Blocking Receive a item from the channel.
     * @note 
     *  - Concurrent blockingRecv on multiple receivers is supported.
     * 
     *  - It will ```BLOCK``` the thread, so it is not recommended to use it in the async context, use it in sync code
     * 
     * @return std::optional<T>, nullopt on the channel is closed
     */
    [[nodiscard]]
    auto blockingRecv() noexcept(std::is_nothrow_move_constructible_v<T>) -> std::optional<T> {
        std::optional<T> value {};
        mChan->receivers.blockingWait([&]() { return mChan->tryRecvInternal(value); });
        if (value) { // Success to recv, wakeup one sender.
            mChan->senders.wakeupOne();
        }
        return value;
    }

    /**
     * @brief Get the refcount of all the receivers.
     * 
     * @return size_t 
     */
    [[nodiscard]]
    auto useCount() const noexcept -> size_t {
        return mChan ? mChan.use_count() : 0;
    }

    auto operator =(const Receiver &other) -> Receiver & = default;
    auto operator =(Receiver &&other) -> Receiver & = default;

    // Check the receiver is valid
    explicit operator bool() const noexcept {
        return bool(mChan);   
    }
private:
    explicit Receiver(detail::ChanReceiver<T> chan) : mChan(std::move(chan)) {}

    detail::ChanReceiver<T> mChan;
template <Sendable U>
friend auto channel(size_t capacity) -> Pair<U>;
template <Sendable U>
friend class WeakReceiver;
};

/**
 * @brief The weak receiver, it doesn't prevent the channel from being closed.
 * 
 * @tparam T The item type to be received.
 */
template <Sendable T>
class WeakReceiver final {
public:
    WeakReceiver() = default;
    WeakReceiver(const WeakReceiver &) = default;
    WeakReceiver(WeakReceiver &&) = default;
    ~WeakReceiver() = default;

    auto close() noexcept -> void {
        mChan.reset();
    }

    // Try upgrade the weak to strong
    auto lock() -> Receiver<T> {
        return Receiver<T> {mChan.lock()};
    }
private:
    detail::ChanWeakReceiver<T> mChan;
};

/**
 * @brief Make a channel for multi producer and multi consumer.
 * 
 * @tparam T The item type to be sent and received. (must be moveable)
 * @param capacity The capacity of the channel. (abort on 0), default on SIZET_MAX (no limit)
 * @return Pair<T> 
 */
template <Sendable T>
inline auto channel(size_t capacity = std::numeric_limits<size_t>::max()) -> Pair<T> {
    ILIAS_ASSERT(capacity > 0, "The capacity of the channel must be greater than 0.");
    auto ptr = new detail::Channel<T> {capacity};
    return {
        .sender = Sender<T> {
            detail::ChanSender<T> {
                ptr, detail::ChanSenderDeleter {}
            }
        },
        .receiver = Receiver<T> {
            detail::ChanReceiver<T> {
                ptr, detail::ChanReceiverDeleter {}
            }
        }
    };
}

} // namespace mpmc

ILIAS_NS_END
