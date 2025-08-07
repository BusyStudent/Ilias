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

#include <ilias/sync/detail/queue.hpp>
#include <ilias/task/task.hpp>
#include <concepts>
#include <memory>
#include <limits>
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
        ILIAS_ASSERT(senderCount == 0);
    }

    size_t capacity = 0; // The capacity of the channel.
    size_t senderCount = 0; // The number of senders.
    sync::WaitQueue senders;
    sync::WaitQueue receiver;
    std::deque<T> queue; // For storing the items.
    bool receiverClosed = false;
};

template <typename T>
class SendAwaiter final : public sync::WaitAwaiter<SendAwaiter<T> > {
public:
    SendAwaiter(Channel<T> *c, T value) : sync::WaitAwaiter<SendAwaiter<T> >(c->senders), mChan(c), mValue(std::move(value)) {};
    SendAwaiter(SendAwaiter &&) = default;

    auto await_ready() -> bool {
        if (mChan->receiverClosed) {
            return true; // The receiver is closed, so we can't send any more.
        }
        if (mChan->queue.size() < mChan->capacity) {
            return true; // Have space, 
        }
        return false; // Wait the queue has space.
    }

    auto await_resume() -> Result<void, T> {
        if (mChan->receiverClosed) {
            return Err(std::move(mValue));
        }
        mChan->queue.emplace_back(std::move(mValue));
        mChan->receiver.wakeupOne();
        return {};
    }
private:
    Channel<T> *mChan;
    T mValue;
};

template <typename T>
class ReceiveAwaiter final : public sync::WaitAwaiter<ReceiveAwaiter<T> > {
public:
    ReceiveAwaiter(Channel<T> *c) : sync::WaitAwaiter<ReceiveAwaiter<T> >(c->receiver), mChan(c) { }
    ReceiveAwaiter(ReceiveAwaiter &&) = default;

    auto await_ready() -> bool {
        if (!mChan->queue.empty()) {
            return true; // Have data,
        }
        if (mChan->senderCount == 0) {
            return true; // No sender, so we can't receive any more.
        }
        return false;
    }

    auto await_resume() -> std::optional<T> {
        if (mChan->queue.empty()) {
            return std::nullopt;
        }
        auto value = std::move(mChan->queue.front());
        mChan->queue.pop_front();
        mChan->senders.wakeupOne();
        return value;
    }
private:
    Channel<T> *mChan;
};

template <typename T>
using ChanRef = std::shared_ptr<Channel<T> >;

} // namespace detail

/**
 * @brief The mpsc sender class. This class is used to send data to the channel. (copy & move able)
 * 
 * @tparam T The item type to be sent.
 */
template <typename T>
class Sender final {
public:
    Sender() = default;
    Sender(Sender &&) = default;
    Sender(const Sender &other) : Sender(other.mChan) {}
    ~Sender() { close(); }

    explicit Sender(detail::ChanRef<T> chan) : mChan(std::move(chan)) {
        if (mChan) {
            mChan->senderCount++;
        }
    }

    /**
     * @brief Close the sender
     * 
     */
    auto close() noexcept -> void {
        if (!mChan) {
            return;
        }
        mChan->senderCount--;
        if (mChan->senderCount == 0) {
            mChan->receiver.wakeupAll();
        }
        mChan.reset();
    }

    /**
     * @brief Check if the channel is closed. we can't send any more data
     * 
     * @return true 
     * @return false 
     */
    auto isClosed() const noexcept -> bool {
        return !mChan || mChan->receiverClosed;
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

    auto operator =(Sender &&other) -> Sender & {
        close();
        mChan = std::move(other.mChan);
        return *this;
    }

    auto operator =(const Sender &other) -> Sender & {
        close();
        mChan = other.mChan;
        if (mChan) {
            mChan->senderCount++;
        }
        return *this;
    }

    explicit operator bool() const noexcept {
        return bool(mChan);
    }
private:
    detail::ChanRef<T> mChan;
};

/**
 * @brief The mpsc receiver class. This class is used to receive data from the channel. (only moveable)
 * 
 * @tparam T The item type to be received.
 */
template <typename T>
class Receiver final {
public:
    Receiver() = default;
    Receiver(const Receiver &) = delete;
    Receiver(Receiver &&) = default;
    ~Receiver() { close(); }

    explicit Receiver(detail::ChanRef<T> chan) : mChan(std::move(chan)) {}

    auto close() noexcept -> void {
        if (mChan) {
            mChan->receiverClosed = true;
            mChan->senders.wakeupAll();
            mChan.reset();
        }
    }

    auto isClosed() const noexcept -> bool {
        return !mChan || mChan->senderCount == 0;
    }

    /**
     * @brief Receive a item from the channel.
     * 
     * @return std::optional<T>, nullopt on the channel is closed
     */
    [[nodiscard]]
    auto recv() const noexcept {
        return detail::ReceiveAwaiter<T>(mChan.get());
    }

    auto operator =(const Receiver &other) = delete;
    auto operator =(Receiver &&other) -> Receiver & {
        close();
        mChan = std::move(other.mChan);
        return *this;
    }

    explicit operator bool() const noexcept {
        return bool(mChan);   
    }
private:
    detail::ChanRef<T> mChan;
};

template <typename T>
struct Pair {
    Sender<T> sender;
    Receiver<T> receiver;
};

/**
 * @brief Make a channel for multi producer and single consumer.
 * 
 * @tparam T The item type to be sent and received. (must be moveable)
 * @param capacity The capacity of the channel. (abort on 0)
 * @return Pair<T> 
 */
template <std::movable T>
inline auto channel(size_t capacity) -> Pair<T> {
    ILIAS_ASSERT_MSG(capacity > 0, "The capacity of the channel must be greater than 0.");
    auto ptr = std::make_shared<detail::Channel<T> >(capacity);
    return {
        .sender = Sender<T>(ptr),
        .receiver = Receiver<T>(ptr)
    };
}

} // namespace mpsc

ILIAS_NS_END