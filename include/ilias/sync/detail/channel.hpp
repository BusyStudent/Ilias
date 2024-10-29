/**
 * @file channel.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides a channel template for coroutines communication.
 * @version 0.1
 * @date 2024-10-24
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/detail/refptr.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/cancellation_token.hpp>
#include <ilias/error.hpp>
#include <ilias/log.hpp>
#include <limits>
#include <queue>
#include <list>

ILIAS_NS_BEGIN

namespace channel_impl {

/**
 * @brief Concept for a queue that the task awaiter on it
 * 
 * @tparam T 
 */
template <typename T>
concept AwaitQueue = requires(T t) {
    typename T::Token; //< The tomken used to Remove the await, it will be invalidated after the task is scheduled.
    t.empty();    //< Check if the queue is empty.
    t.wakeup();  //< Wake up 
    t.remove(typename T::Token{ }); //< Remove the given awaiter by token.
    t.push(std::declval<void (*)(void *)>(), nullptr); //< Push a task to the queue.}
};

/**
 * @brief A Waiting queue only has one task in it.
 * 
 */
class SingleQueue {
public:
    struct Token { };

    SingleQueue() = default;

    auto empty() const noexcept {
        return !mFn;
    }

    auto wakeup() noexcept {
        ILIAS_ASSERT(!empty());
        mFn(mArgs);
        mFn = nullptr;
        mArgs = nullptr;
    }

    auto push(void (*fn)(void*), void *args) noexcept {
        ILIAS_ASSERT(empty());
        mFn = fn;
        mArgs = args;
        return Token{ };
    }

    auto remove(Token) {
        ILIAS_ASSERT(!empty());
        mFn = nullptr;
        mArgs = nullptr;
    }
private:
    void (*mFn)(void *) = nullptr;
    void *mArgs = nullptr;
};

/**
 * @brief The waiting queue for multiple tasks.
 * 
 */
class MultiQueue {
public:
    struct Token {
        std::list<
            std::pair<void (*)(void *), void *>
        >::iterator it;
    };

    MultiQueue() = default;

    auto empty() const noexcept {
        return mItems.empty();
    }

    auto wakeup() noexcept {
        ILIAS_ASSERT(!empty());
        auto [fn, args] = mItems.front();
        mItems.pop_front();
        fn(args);
    }

    auto push(void (*fn)(void*), void *args) noexcept {
        mItems.emplace_back(fn, args);
        return Token{ .it = --mItems.end() };
    }

    auto remove(Token token) {
        ILIAS_ASSERT(!empty());
        mItems.erase(token.it);
    }
private:
    std::list<
        std::pair<void (*)(void *), void *>
    > mItems;
};

class EmptyBase {
public:
    EmptyBase() = default;
};

class NonCopyable {
public:
    NonCopyable() = default;
    NonCopyable(const NonCopyable &) = delete;
};

/**
 * @brief The Generic channel template
 * 
 * @tparam T 
 * @tparam SenderQueue 
 * @tparam RecvQueue 
 */
template <typename T, AwaitQueue _SenderQueue, AwaitQueue _ReceiverQueue>
class Channel {
public:
    using ValueType = T;
    using SenderQueue = _SenderQueue;
    using ReceiverQueue = _ReceiverQueue;

    Channel(size_t capacity = std::numeric_limits<size_t>::max()) : mCapacity(capacity) { }
    Channel(const Channel &) = delete;
    ~Channel() {
        ILIAS_ASSERT_MSG(mSenderCount == 0 && mReceiverCount == 0, "Reference count is not zero");
        ILIAS_ASSERT_MSG(mSenderQueue.empty() && mReceiverQueue.empty(), "Should no-one waiting on the channel");
    }

    // Reference counting
    auto senderRef() noexcept {
        ++mSenderCount;
    }
    auto senderDeref() noexcept {
        --mSenderCount;
        if (mSenderCount != 0) {
            return;
        }
        // Wakeup all the receivers, because the channel is broken.
        while (!mReceiverQueue.empty()) {
            mReceiverQueue.wakeup();
        }
        if (mReceiverCount == 0) {
            delete this; //< No one take it, so delete it.
        }
    }

    auto receiverRef() noexcept {
        ++mReceiverCount;
    }
    auto receiverDeref() noexcept {
        --mReceiverCount;
        if (mReceiverCount != 0) {
            return;
        }
        // Wakeup all the senders, because the channel is broken.
        while (!mSenderQueue.empty()) {
            mSenderQueue.wakeup();
        }
        if (mSenderCount == 0) {
            delete this; //< No one take it, so delete it.
        }
    }

    std::queue<T> mQueue;
    SenderQueue   mSenderQueue;
    ReceiverQueue mReceiverQueue;
    size_t        mCapacity;
    size_t        mSenderCount = 0;
    size_t        mReceiverCount = 0;
};


template <typename ChannelT>
class SendAwaiter {
public:
    using Queue = typename ChannelT::SenderQueue;
    using Token = typename Queue::Token;
    using ValueType = typename ChannelT::ValueType;

    SendAwaiter(ChannelT *channel, ValueType &&value) : mChannel(channel), mValue(std::move(value)) { }

    auto await_ready() const -> bool {
        return mChannel->mQueue.size() < mChannel->mCapacity ||   //< Has space to send
               mChannel->mReceiverCount == 0;                     //< No receiver (broken)
    }

    auto await_suspend(TaskView<> caller) -> void {
        mCaller = caller;
        mToken = mChannel->mSenderQueue.push(onComplete, this);
        mHasToken = true;
        mReg = mCaller.cancellationToken().register_(onCancel, this);
    }

    auto await_resume() const -> Result<void> {
        ILIAS_ASSERT_MSG(!mHasToken, "Should already completed or canceled");
        if (mIsCanceled) {
            return Unexpected(Error::Canceled);
        }
        if (mChannel->mReceiverCount == 0) {
            return Unexpected(Error::ChannelBroken);
        }
        ILIAS_ASSERT(mChannel->mQueue.size() < mChannel->mCapacity); //< Should has space
        mChannel->mQueue.emplace(std::move(mValue));
        if (!mChannel->mReceiverQueue.empty()) { //< If a receiver are waiting, wakeup it
            mChannel->mReceiverQueue.wakeup();
        }
        return {};
    }
private:
    static auto onComplete(void *_self) -> void {
        auto self = static_cast<SendAwaiter *>(_self);
        self->mHasToken = false;
        self->mCaller.schedule();
    }

    static auto onCancel(void *_self) -> void {
        auto self = static_cast<SendAwaiter *>(_self);
        if (!self->mHasToken) { //< Completed already
            return;
        }
        ILIAS_TRACE("Sender", "Cancel {}", _self);
        self->mChannel->mSenderQueue.remove(self->mToken);
        self->mIsCanceled = true;
        self->mHasToken = false;
        self->mCaller.schedule();
    }

    ChannelT *mChannel;
    ValueType &&mValue; //< The value we want to send
    TaskView<> mCaller;
    Token      mToken;
    CancellationToken::Registration mReg;
    bool mIsCanceled = false;
    bool mHasToken = false;
};

template <typename ChannelT>
class RecvAwaiter {
public:
    using Queue = typename ChannelT::ReceiverQueue;
    using Token = typename Queue::Token;
    using ValueType = typename ChannelT::ValueType;

    RecvAwaiter(ChannelT *channel) : mChannel(channel) { }

    auto await_ready() const -> bool {
        return mChannel->mQueue.size() > 0 || mChannel->mSenderCount == 0; //< Has item to receive or no sender (broken)
    }

    auto await_suspend(TaskView<> caller) -> void {
        mCaller = caller;
        mToken = mChannel->mReceiverQueue.push(onComplete, this);
        mHasToken = true;
        mReg = mCaller.cancellationToken().register_(onCancel, this);
    }

    auto await_resume() const -> Result<ValueType> {
        ILIAS_ASSERT_MSG(!mHasToken, "Should already completed or canceled");
        if (mIsCanceled) {
            return Unexpected(Error::Canceled);
        }
        if (mChannel->mSenderCount == 0 && mChannel->mQueue.empty()) {
            //< No sender and no item in the queue (broken)
            return Unexpected(Error::ChannelBroken);
        }
        ILIAS_ASSERT(mChannel->mQueue.size() > 0); //< Should has item
        auto value = std::move(mChannel->mQueue.front());
        mChannel->mQueue.pop();
        if (!mChannel->mSenderQueue.empty()) { //< If a sender are waiting, wakeup it up
            mChannel->mSenderQueue.wakeup();
        }
        return value;
    }
private:
    static auto onComplete(void *_self) -> void {
        auto self = static_cast<RecvAwaiter *>(_self);
        self->mHasToken = false;
        self->mCaller.schedule();
    }

    static auto onCancel(void *_self) -> void {
        auto self = static_cast<RecvAwaiter *>(_self);
        if (!self->mHasToken) { //< Completed already
            return;
        }
        ILIAS_TRACE("Receiver", "Cancel {}", _self);
        self->mChannel->mReceiverQueue.remove(self->mToken);
        self->mIsCanceled = true;
        self->mHasToken = false;
        self->mCaller.schedule();
    }

    ChannelT *mChannel;
    TaskView<> mCaller;
    Token      mToken;
    CancellationToken::Registration mReg;
    bool mIsCanceled = false;
    bool mHasToken = false;
};

/**
 * @brief For Sender
 * 
 * @tparam T The channel type
 * @tparam Copyable Should the sender be copyable ?
 */
template <typename ChannelT, bool Copyable>
class Sender : public std::conditional_t<Copyable, EmptyBase, NonCopyable> {
public:
    using ValueType = typename ChannelT::ValueType;

    explicit Sender(ChannelT *ptr) : mPtr(ptr) { }

    Sender() = default;
    Sender(std::nullptr_t) { }
    Sender(Sender &&) = default;

    /**
     * @brief Close the current sender
     * 
     */
    auto close() -> void { mPtr = nullptr; }

    /**
     * @brief Get the capacity of the channel (how many items can be pushed)
     * 
     * @return auto 
     */
    auto capacity() const noexcept {
        return mPtr->mCapacity - mPtr->mQueue.size();
    }

    /**
     * @brief Check the sender can send item?
     * 
     * @return auto 
     */
    auto isBroken() const noexcept {
        return !mPtr || mPtr->mReceiverCount == 0;
    }

    /**
     * @brief Send an item to the channel
     * 
     * @param value 
     * @return SendAwaiter<ChannelT> 
     */
    auto send(ValueType &&value) -> SendAwaiter<ChannelT> {
        ILIAS_ASSERT(mPtr != nullptr);
        return SendAwaiter<ChannelT>(mPtr.get(), std::move(value));  
    }

    /**
     * @brief Try to send an item to the channel
     * 
     * @param value 
     * @return Result<void> 
     */
    auto trySend(ValueType &&value) -> Result<void> {
        if (isBroken()) {
            return Unexpected(Error::ChannelBroken);
        }
        if (mPtr->mQueue.size() >= mPtr->mCapacity) {
            return Unexpected(Error::ChannelFull);
        }
        mPtr->mQueue.emplace(std::move(value));
        if (!mPtr->mReceiverQueue.empty()) {
            mPtr->mReceiverQueue.wakeup();
        }
        return {};
    }

    /**
     * @brief Check the sender is still valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() noexcept {
        return mPtr != nullptr;
    }
private:
    struct RefTraits {
        static auto ref(ChannelT *channelPtr) {
            channelPtr->senderRef();
        }
        static auto deref(ChannelT *channelPtr) {
            channelPtr->senderDeref();
        }
    };

    detail::RefPtr<ChannelT, RefTraits> mPtr;
};

/**
 * @brief For Receiver
 * 
 * @tparam ChannelT The channel type
 * @tparam Cooyable Should the receiver class be copyable?
 */
template <typename ChannelT, bool Cooyable>
class Receiver : public std::conditional_t<Cooyable, EmptyBase, NonCopyable> {
public:
    using ValueType = typename ChannelT::ValueType;

    explicit Receiver(ChannelT *ptr) : mPtr(ptr) { }

    Receiver() = default;
    Receiver(std::nullptr_t) { }
    Receiver(Receiver &&) = default;

    /**
     * @brief  Close the current receiver
     * 
     */
    auto close() -> void { mPtr = nullptr; }

    /**
     * @brief Get the capacity of the channel (how many items can be popped)
     * 
     * @return auto 
     */
    auto capacity() const noexcept {
        return mPtr->mQueue.size();
    }

    /**
     * @brief Check the receiver can receive item?
     * 
     * @return auto 
     */
    auto isBroken() const noexcept {
        return !mPtr || mPtr->mSenderCount == 0;
    }

    /**
     * @brief Recieve an item from the channel
     * 
     * @return auto 
     */
    auto recv() {
        return RecvAwaiter<ChannelT>(mPtr.get());
    }

    /**
     * @brief Try recieve an item from the channel
     * 
     * @return Result<ValueType> 
     */
    auto tryRecv() -> Result<ValueType> {
        if (isBroken()) {
            return Unexpected(Error::ChannelBroken);
        }
        if (mPtr->mQueue.empty()) {
            return Unexpected(Error::ChannelEmpty);
        }
        auto value = std::move(mPtr->mQueue.front());
        mPtr->mQueue.pop();
        if (!mPtr->mSenderQueue.empty()) {
            mPtr->mSenderQueue.wakeup();
        }
        return value;
    }

    auto operator co_await() {
        return RecvAwaiter<ChannelT>(mPtr.get());
    }

    /**
     * @brief Check the receiver is still valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() noexcept {
        return !mPtr;
    }
private:
    struct RefTraits {
        static auto ref(ChannelT *channelPtr) noexcept {
            channelPtr->receiverRef();
        }
        static auto deref(ChannelT *channelPtr) noexcept {
            channelPtr->receiverDeref();
        }
    };

    detail::RefPtr<ChannelT, RefTraits> mPtr;
};

} // namespace detail

ILIAS_NS_END