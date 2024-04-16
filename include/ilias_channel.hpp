#pragma once

#include "ilias_task.hpp"
#include <queue>

ILIAS_NS_BEGIN

template <typename T>
class Sender;
template <typename T>
class Receiver;

/**
 * @brief A multi producer single customer channel
 * 
 * @tparam T 
 */
template <typename T>
class Channel {
public:
    using handle_type = std::coroutine_handle<>;

    /**
     * @brief Create a channel
     * 
     * @return std::pair<Sender<T>, Receiver<T> > 
     */
    static auto make(size_t c = 32) -> std::pair<Sender<T>, Receiver<T> >;
private:    
    Channel() = default;
    /**
     * @brief Wakeup the sender on waiting list
     * 
     */
    auto _wakeupSender() -> void;
    /**
     * @brief Wakeup the receiver on waiting list
     * 
     */
    auto _wakeupReceiver() -> void;

    std::queue<handle_type> mSenderWaiters; //< The sender handle
    handle_type mReceiverWaiter; //< The recv handle
    std::queue<Result<T> > mQueue;
    size_t mCapicity = 0; //< The capicity of the channel
    size_t mSenderCount = 0;
    size_t mRefcount = 0; //< The memory refcount
    bool mReceiverClosed = false;
template <typename U>
friend class Sender;
template <typename U>
friend class Receiver;
};

/**
 * @brief The sender of the mpsc channel
 * 
 * @tparam T 
 */
template <typename T>
class Sender {
public:
    Sender(Channel<T> &channel);
    Sender(const Sender &);
    Sender(Sender &&);
    ~Sender();

    /**
     * @brief Send a value to the channel, it will block if the channel is full
     * 
     * @param result 
     * @return Task<void> 
     */
    auto send(Result<T> v) -> Task<void>;
    /**
     * @brief Try Send a value to the channel, it will return ChannelFull when full, Broken on peer closed
     * 
     * @param v 
     * @return Result<void> 
     */
    auto trySend(Result<T> v) -> Result<void>;
    /**
     * @brief Close the sender
     * 
     */
    auto close() -> void;
private:
    Channel<T> *mChannel = nullptr;
};

/**
 * @brief The receiver of the mpsc channel
 * 
 * @tparam T 
 */
template <typename T>
class Receiver {
public:
    Receiver(Channel<T> &channel);
    Receiver(const Receiver &) = delete;
    Receiver(Receiver &&);
    ~Receiver();

    /**
     * @brief Receive a value from the channel, it will block if the channel is empty
     * 
     * @return Task<T> 
     */
    auto recv() -> Task<T>;
    /**
     * @brief Try Receive a value from the channel, it will return ChannelEmpty when empty, Broken on peer closed
     * 
     * @return Result<T> 
     */
    auto tryRecv() -> Result<T>;
    /**
     * @brief Close the receiver
     * 
     */
    auto close() -> void;
private:
    Channel<T>* mChannel = nullptr;
};

// --- Sender Impl
template <typename T>
inline Sender<T>::Sender(Channel<T> &channel) : mChannel(&channel) {
    channel.mSenderCount += 1;
    channel.mRefcount += 1;
}
template <typename T>
inline Sender<T>::Sender(const Sender &sender) : mChannel(sender.mChannel) {
    if (mChannel) {
        mChannel->mSenderCount += 1;
        mChannel->mRefcount += 1;
    }
}
template <typename T>
inline Sender<T>::Sender(Sender &&sender) : mChannel(sender.mChannel) {
    sender.mChannel = nullptr;
}
template <typename T>
inline Sender<T>::~Sender() {
    close();
}
template <typename T>
inline auto Sender<T>::close() -> void {
    if (!mChannel) {
        return;
    }
    mChannel->mSenderCount -= 1;
    mChannel->_wakeupReceiver();
    mChannel->mRefcount -= 1;
    if (mChannel->mRefcount == 0) {
        ILIAS_ASSERT(mChannel->mReceiverClosed && mChannel->mSenderCount == 0);
        // shoud no receiver and sender alive
        delete mChannel;
    }
    mChannel = nullptr;
}
template <typename T>
inline auto Sender<T>::send(Result<T> value) -> Task<void> {
    auto self = co_await GetPromise();
    while (mChannel->mQueue.size() == mChannel->mCapicity) {
        if (!mChannel || mChannel->mReceiverClosed) {
            co_return Unexpected(Error::ChannelBroken);
        }
        // Push self to waiting queue and wakeup the receiver in event loop
        mChannel->mSenderWaiters.emplace(self->handle());
        // Must in Event Loop !!!, avoid it resume us when we are not suspend
        if (mChannel->mReceiverWaiter) {
            self->eventLoop()->resumeHandle(mChannel->mReceiverWaiter);
            mChannel->mReceiverWaiter = nullptr;
        }
        co_await std::suspend_always();
    }
    if (!mChannel || mChannel->mReceiverClosed) {
        co_return Unexpected(Error::ChannelBroken);
    }
    mChannel->mQueue.emplace(std::move(value));
    mChannel->_wakeupReceiver();
    co_return Result<void>();
}

template <typename T>
inline auto Sender<T>::trySend(Result<T> value) -> Result<void> {
    if (!mChannel || mChannel->mReceiverClosed) {
        return Unexpected(Error::ChannelBroken);
    }
    if (mChannel->mQueue.size() == mChannel->mCapicity) {
        return Unexpected(Error::ChannelFull);        
    }
    mChannel->mQueue.emplace(std::move(value));
    if (mChannel->mWaiter) {
        // Some one wating on it
        mChannel->mWaiter.resume();
        mChannel->mWaiter = nullptr;
    }
    return Result<void>();
}

// --- Receiver Impl
template <typename T>
inline Receiver<T>::Receiver(Channel<T> &channel) : mChannel(&channel) {
    mChannel->mRefcount += 1;
}
template <typename T>
inline Receiver<T>::Receiver(Receiver<T> &&r) : mChannel(r.mChannel) {
    r.mChannel = nullptr;
}
template <typename T>
inline Receiver<T>::~Receiver() {
    close();
}

template <typename T>
inline auto Receiver<T>::close() -> void {
    if (!mChannel) {
        return;
    }
    mChannel->mReceiverClosed = true;
    mChannel->_wakeupSender();
    mChannel->mRefcount -= 1;
    if (mChannel->mRefcount == 0) {
        ILIAS_ASSERT(mChannel->mSenderCount == 0);
        // shoud no sender alive
        delete mChannel;
    }
    mChannel = nullptr;
}
template <typename T>
inline auto Receiver<T>::recv() -> Task<T> {
    auto self = co_await GetPromise();
    while (!self->isCanceled()) {
        if (!mChannel) {
            co_return Unexpected(Error::ChannelBroken);
        }
        if (mChannel->mQueue.empty() && mChannel->mSenderCount == 0) {
            co_return Unexpected(Error::ChannelBroken);
        }
        if (mChannel->mQueue.empty()) {
            mChannel->mReceiverWaiter = self->handle();
            co_await std::suspend_always();
            continue;
        }
        auto value = std::move(mChannel->mQueue.front());
        mChannel->mQueue.pop();
        mChannel->_wakeupSender();
        co_return value;
    }
    co_return Unexpected(Error::Canceled);
}
template <typename T>
inline auto Receiver<T>::tryRecv() -> Result<T> {
    if (!mChannel) {
        return Unexpected(Error::ChannelBroken);
    }
    if (mChannel->mQueue.empty() && mChannel->mSenderCount == 0) {
        return Unexpected(Error::ChannelBroken);
    }
    if (mChannel->mQueue.empty()) {
        return Unexpected(Error::ChannelEmpty);
    }
    auto value = std::move(mChannel->mQueue.front());
    mChannel->mQueue.pop();
    mChannel->_wakeupSender();
    return value;
}

template <typename T>
inline auto Channel<T>::make(size_t size) -> std::pair<Sender<T>, Receiver<T> > {
    auto c = new Channel<T>;
    c->mCapicity = size;
    return std::make_pair(Sender<T>(*c), Receiver<T>(*c));
}
template <typename T>
inline auto Channel<T>::_wakeupReceiver() -> void {
    if (mReceiverWaiter) {
        mReceiverWaiter.resume();
        mReceiverWaiter = nullptr;
    }
}
template <typename T>
inline auto Channel<T>::_wakeupSender() -> void {
    if (!mSenderWaiters.empty()) {
        auto h = mSenderWaiters.front();
        mSenderWaiters.pop();
        h.resume();
    }
}

ILIAS_NS_END