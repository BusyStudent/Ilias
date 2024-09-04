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

#include <ilias/cancellation_token.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
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

template <typename T>
concept Sendable = std::movable<T> && (!std::is_reference_v<T>);

template <typename T>
class Sender;
template <typename T>
class Receiver;

template <typename T>
struct Pair {
    Sender<T>   sender;
    Receiver<T> receiver;
};

namespace detail {

/**
 * @brief The data block for the channel, the reciever take the ownership of the data block
 * 
 * @tparam T 
 */
template <typename T>
class DataBlock {
public:
    ~DataBlock() {
        ILIAS_TRACE("Oneshot::Channel", "Sender::close() {}", (void*) this);
        ILIAS_ASSERT(!suspendedTask); //< MUST no-one waiting on the channel
        if (sender) {
            sender->mPtr = nullptr;
        }
    }

    TaskView<> suspendedTask; //< The task that is suspended on the recv operation
    Sender<T> *sender = nullptr;
    std::optional<T> value;
    bool sended = false; //< Does the sender has sended the value
};

/**
 * @brief Do the recv operation
 * 
 * @tparam T 
 */
template <typename T>
class RecvAwaiter {
public:
    RecvAwaiter(DataBlock<T> *ptr) : mPtr(ptr) { }

    auto await_ready() const -> bool { 
        // Already sended or the sender is null (broken)
        return mPtr->sended || mPtr->sender == nullptr;
    }

    auto await_suspend(TaskView<> caller) -> void {
        mPtr->suspendedTask = caller;
        mReg = caller.cancellationToken().register_([this]() {
            mPtr->suspendedTask.schedule();
            mPtr->suspendedTask = nullptr; //< Clear the suspended task
            mIsCanceled = true;
        });
    }

    auto await_resume() -> Result<T> {
        mPtr->suspendedTask = nullptr;
        if (mPtr->value) {
            return std::move(*mPtr->value);
        }
        // Error happened
        if (mIsCanceled) {
            return Unexpected(Error::Canceled);
        }
        return Unexpected(Error::ChannelBroken);
    }
private:
    DataBlock<T> *mPtr;
    CancellationToken::Registration mReg;
    bool mIsCanceled = false;
};

} // namespace detail


/**
 * @brief The receiver of the channel (move only)
 * 
 * @tparam T 
 */
template <typename T>
class Receiver {
public:
    Receiver() = default;

    Receiver(std::nullptr_t) { }

    /**
     * @brief Check if the channel is closed
     * 
     * @return true 
     * @return false 
     */
    auto isClosed() const -> bool {
        return mPtr->sender == nullptr;
    }

    /**
     * @brief Close the channel
     * 
     */
    auto close() -> void {
        mPtr.reset();
    }

    /**
     * @brief Get the value from the channel
     * 
     * @return Task<T> 
     */
    [[nodiscard("DO NOT FORGET TO USE co_await!!!")]]
    auto recv() -> Task<T> {
        co_return co_await detail::RecvAwaiter<T>(mPtr.get());
    }

    auto tryRecv() -> Result<T> {
        if (isClosed()) {
            return Unexpected(Error::ChannelBroken);
        }
        if (!mPtr->sended) {
            return Unexpected(Error::ChannelEmpty);
        }
        return std::move(*mPtr->value);
    }

    auto operator <=>(const Receiver &) const = default;

    /**
     * @brief Awaits the value from the channel
     * 
     * @return co_await 
     */
    auto operator co_await() -> detail::RecvAwaiter<T> {
        return detail::RecvAwaiter<T>(mPtr.get());
    }

    /**
     * @brief Check if the channel is open
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return !isClosed();
    }
private:
    Receiver(std::unique_ptr<detail::DataBlock<T> > &&ptr) : mPtr(std::move(ptr)) { }

    std::unique_ptr<detail::DataBlock<T> > mPtr;
template <Sendable U>
friend auto channel() -> Pair<U>;
};

/**
 * @brief The sender of the channel (move only)
 * 
 * @tparam T 
 */
template <typename T>
class Sender {
public:
    Sender() = default;

    Sender(std::nullptr_t) { }

    Sender(const Sender &) = delete;

    Sender(Sender &&other) : mPtr(other.mPtr) {
        bind();
        other.mPtr = nullptr;
    }

    ~Sender() {
        close();
    }

    /**
     * @brief Close the channel
     * 
     * @return auto 
     */
    auto close() -> void {
        if (!mPtr) {
            return;
        }
        ILIAS_TRACE("Oneshot::Channel", "Sender::close() {}", (void*) mPtr);
        mPtr->sender = nullptr;
        if (mPtr->suspendedTask) {
            mPtr->suspendedTask.schedule();
        }
        mPtr = nullptr;
    }

    /**
     * @brief Send a value to the channel
     * 
     * @tparam U 
     * @param value 
     * @return Result<void> 
     */
    template <typename U>
    auto send(U &&value) -> Result<void> {
        if (!mPtr || mPtr->sended) {
            return Unexpected(Error::ChannelBroken);
        }
        mPtr->value.emplace(std::forward<U>(value));
        mPtr->sended = true;
        if (mPtr->suspendedTask) {
            mPtr->suspendedTask.schedule();
        }
        return {};
    }

    /**
     * @brief Check if the channel is closed
     * 
     * @return true 
     * @return false 
     */
    auto isClosed() const -> bool {
        return mPtr == nullptr;
    }

    /**
     * @brief Get the capacity of the channel, the number of values that can be sent
     * 
     * @return size_t 
     */
    auto capacity() const -> size_t {
        if (isClosed() || mPtr->sended) {
            return 0;
        }
        return 1;
    }

    auto operator =(const Sender &) = delete;

    auto operator =(Sender &&other) -> Sender & {
        if (this == &other) {
            return *this;
        }
        close();
        mPtr = other.mPtr;
        bind();
        other.mPtr = nullptr;
    }

    auto operator <=>(const Sender &) const = default;

    explicit operator bool() const noexcept {
        return !isClosed();
    }
private:
    Sender(detail::DataBlock<T> *ptr) : mPtr(ptr) {
        bind();
    }

    auto bind() {
        if (mPtr) {
            mPtr->sender = this;
        }
    }

    detail::DataBlock<T> *mPtr = nullptr;
friend class detail::DataBlock<T>;
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
    auto uniquePtr = std::make_unique<detail::DataBlock<T> >();
    auto ptr = uniquePtr.get();

    return Pair<T>(Sender<T>(ptr), Receiver<T>(std::move(uniquePtr)));
}

} // namespace oneshot

ILIAS_NS_END