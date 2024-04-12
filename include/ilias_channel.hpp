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
    /**
     * @brief Create a channel
     * 
     * @return std::pair<Sender<T>, Receiver<T> > 
     */
    static auto make(size_t c = 32) -> std::pair<Sender<T>, Receiver<T> >;
private:
    Channel() = default;

    std::queue<T> mQueue;
    std::coroutine_handle<> mWaiter; //< The watier handle
    size_t mCapacity = 0; //< The capacity of the channel
    bool mReceiverClosed = false;
    int  mSenderCount = 0;
template <typename U>
friend class Sender;
template <typename U>
friend class Receiver;
};

template <typename T>
class Sender {
public:
    Sender(Channel<T> &channel);
    Sender(const Sender &);
    Sender(Sender &&);
    ~Sender();

    /**
     * @brief Send a value to the channel
     * 
     * @param result 
     * @return Task<void> 
     */
    auto send(T &&value) -> Task<void>;
    auto trySend(T &&value) -> Result<void>;
    /**
     * @brief Close the sender
     * 
     */
    auto close() -> void;
private:
    Channel<T> *mChannel = nullptr;
};

template <typename T>
class Receiver {
public:
    Receiver(Channel<T> &channel);
    Receiver(const Receiver &) = delete;
    Receiver(Receiver &&);
    ~Receiver();

    auto recv() -> Task<T>;
    auto tryRecv() -> Result<T>;
    auto close() -> void;
private:
    Channel<T>* mChannel = nullptr;
};

// --- Sender Impl
template <typename T>
inline Sender<T>::Sender(Channel<T> &channel) : mChannel(&channel) {
    channel.mSenderCount += 1;
}
template <typename T>
inline Sender<T>::Sender(const Sender &sender) : mChannel(sender.mChannel) {
    if (mChannel) {
        mChannel->mSenderCount += 1;
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
    if (mChannel->mSenderCount == 0 && mChannel->mReceiverClosed) {
        delete mChannel;
    }
    mChannel = nullptr;
}
// TODO:
template <typename T>
inline auto Sender<T>::send(T &&value) -> Task<void> {
    co_return Unexpected(Error::ChannelBroken);
}

template <typename T>
inline auto Sender<T>::trySend(T &&value) -> Result<void> {
    return Unexpected(Error::ChannelBroken);
}


template <typename T>
inline auto Channel<T>::make(size_t size) -> std::pair<Sender<T>, Receiver<T> > {
    auto c = new Channel<T>;
    c->mCapicity = size;
    return std::make_pair(Sender<T>(*c), Receiver<T>(*c));
}

ILIAS_NS_END