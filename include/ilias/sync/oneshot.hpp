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

#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <ilias/result.hpp>
#include <ilias/log.hpp>
#include <optional>
#include <concepts>
#include <memory>

ILIAS_NS_BEGIN


/**
 * @brief Channel for sending one value, the sender and receiver are not copyable, only movable, it is not thread-safe
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
    }

    runtime::CoroHandle receiver; // The caller that is suspended on the recv operation
    std::optional<T> value;
    bool senderClose = false;
    bool valueGot = false;
};

/**
 * @brief Do the recv operation
 * 
 * @tparam T 
 */
template <typename T>
class RecvAwaiter {
public:
    RecvAwaiter(std::shared_ptr<Channel<T> > chan) : mChan(std::move(chan)) { }

    auto await_ready() const -> bool { 
        // Already sended or broken
        return mChan->value.has_value() || mChan->senderClose;
    }

    auto await_suspend(runtime::CoroHandle caller) -> void {
        mChan->receiver = caller;
        mReg.register_<&RecvAwaiter::onStopRequested>(caller.stopToken(), this);
    }

    auto await_resume() -> std::optional<T>{
        ILIAS_ASSERT_MSG(!mChan->valueGot, "Double call on recv ?, value already got");
        ILIAS_ASSERT(!mChan->receiver);

        if (mChan->senderClose) {
            return std::nullopt;
        }
        mChan->valueGot = true;
        return std::move(*mChan->value);
    }
private:
    auto onStopRequested() -> void {
        auto handle = std::exchange(mChan->receiver, nullptr);
        if (handle) { // Not already scheduled
            handle.setStopped();
        }
    }

    std::shared_ptr<Channel<T> > mChan;
    runtime::StopRegistration    mReg;
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
    auto close() -> void {
        return mChan.reset();
    }

    [[nodiscard]]
    auto empty() const -> bool {
        return isEmpty();
    }

    [[nodiscard]]
    auto isClosed() const -> bool {
        return !mChan || mChan->senderClose;
    }

    [[nodiscard]]
    auto isEmpty() const -> bool {
        return !mChan || !mChan->value.has_value();
    }

    /**
     * @brief Get the value from the channel
     * 
     * @return std::optional<T>, nullopt on closed
     */
    [[nodiscard]]
    auto recv() && {
        return detail::RecvAwaiter<T>(std::move(mChan));
    }

    /**
     * @brief Try to recv the value from the channel
     * 
     * @return Result<T, TryRecvError> 
     */
    [[nodiscard]]
    auto tryRecv() -> Result<T, TryRecvError> {
        if (isClosed()) {
            return Err(TryRecvError::Closed);
        }
        if (mChan->value) {
            auto ptr = std::move(mChan);
            return std::move(*ptr->value);
        }
        return Err(TryRecvError::Empty);
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
    Receiver(std::shared_ptr<detail::Channel<T> > ptr) : mChan(std::move(ptr)) { }

    std::shared_ptr<detail::Channel<T> > mChan;
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
    ~Sender() { close(); }

    auto close() -> void {
        if (auto ptr = mWeak.lock(); ptr) {
            ptr->senderClose = true;
            ptr->notify();
        }
        mWeak.reset();
    }

    // Check the sender is closed, it is safe to call on an empty sender (empty as closed)
    [[nodiscard]]
    auto isClosed() -> bool {
        return mWeak.expired();
    }

    // Send the value to the channel, if closed, return the value as error
    [[nodiscard]]
    auto send(T value) const -> Result<void, T> {
        if (auto ptr = mWeak.lock(); ptr) {
            if (ptr->value) {
                return Err(std::move(value));
            }
            ptr->value.emplace(std::move(value));
            ptr->notify();
            return {};
        }
        return Err(std::move(value));
    }

    auto operator =(const Sender &) = delete;
    auto operator =(Sender &&other) -> Sender & = default;
    auto operator <=>(const Sender &) const = default;
private:
    Sender(std::weak_ptr<detail::Channel<T> > ptr) : mWeak(std::move(ptr)) {}

    std::weak_ptr<detail::Channel<T> > mWeak;
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
    auto ptr = std::make_shared<detail::Channel<T> >();
    return {
        .sender = Sender<T>(ptr),
        .receiver = Receiver<T>(ptr)
    };
}

} // namespace oneshot

ILIAS_NS_END