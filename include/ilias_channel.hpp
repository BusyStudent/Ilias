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
    auto _send(Result<T> &&v) -> Task<void>;
    auto _trySend(Result<T> &&v) -> Result<void>;
    auto _recv() -> Task<T>;
    auto _tryRecv() -> Result<T>;
    auto _closeSender() -> void;
    auto _closeReceiver() -> void;
    auto _isBroken() const -> bool;
    auto _isEmpty() const -> bool;
    auto _isFull() const -> bool;

    std::deque<handle_type> mSenderHandles; //< The sender handle
    handle_type mReceiverHandle; //< The recv handle
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
    Sender();
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
    /**
     * @brief Check remote receiver is closed (if current sender is null, it will also return true)
     * 
     * @return true 
     * @return false 
     */
    auto isRemoteClosed() const -> bool;
    /**
     * @brief Assign a moved sender
     * 
     * @return Sender& 
     */
    auto operator =(Sender &&) -> Sender &;
    /**
     * @brief Copy a sender
     * 
     * @return Sender& 
     */
    auto operator =(const Sender &) -> Sender &;
    auto operator =(std::nullptr_t) -> Sender &;
    /**
     * @brief Check this sender is closed
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept;
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
    Receiver();
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
    /**
     * @brief Assigned a moved receiver
     * 
     * @return Receiver& 
     */
    auto operator =(Receiver &&) -> Receiver &;
    auto operator =(std::nullptr_t) -> Receiver &;
    auto operator =(const Receiver &) -> Receiver & = delete;
    /**
     * @brief Check this Receiver is closed
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept;
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
inline Sender<T>::Sender() = default;

template <typename T>
inline auto Sender<T>::close() -> void {
    if (!mChannel) {
        return;
    }
    mChannel->_closeSender();
    mChannel = nullptr;
}
template <typename T>
inline auto Sender<T>::send(Result<T> value) -> Task<void> {
    if (!mChannel) {
        co_return Unexpected(Error::ChannelBroken);
    }
    co_return co_await mChannel->_send(std::move(value));
}

template <typename T>
inline auto Sender<T>::trySend(Result<T> value) -> Result<void> {
    if (!mChannel) {
        return Unexpected(Error::ChannelBroken);
    }
    return mChannel->_trySend(std::move(value));
}

template <typename T>
inline auto Sender<T>::isRemoteClosed() const -> bool {
    if (!mChannel) {
        return true;
    }
    return mChannel->mReceiverClosed;
}
template <typename T>
inline auto Sender<T>::operator =(Sender &&sender) -> Sender & {
    if (this != &sender) {
        close();
        mChannel = sender.mChannel;
        sender.mChannel = nullptr;
    }
    return *this;
}
template <typename T>
inline auto Sender<T>::operator =(const Sender &sender) -> Sender & {
    if (this != &sender) {
        close();
        mChannel = sender.mChannel;
        if (mChannel) {
            mChannel->mSenderCount += 1;
            mChannel->mRefcount += 1;
        }
    }
    return *this;
}
template <typename T>
inline auto Sender<T>::operator =(std::nullptr_t) -> Sender & {
    close();
    return *this;
}
template <typename T>
inline Sender<T>::operator bool() const noexcept {
    return mChannel != nullptr;
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
inline Receiver<T>::Receiver() = default;

template <typename T>
inline auto Receiver<T>::close() -> void {
    if (!mChannel) {
        return;
    }
    mChannel->_closeReceiver();
    mChannel = nullptr;
}
template <typename T>
inline auto Receiver<T>::recv() -> Task<T> {
    if (!mChannel) {
        co_return Unexpected(Error::ChannelBroken);
    }
    co_return co_await mChannel->_recv();
}
template <typename T>
inline auto Receiver<T>::tryRecv() -> Result<T> {
    if (!mChannel) {
        return Unexpected(Error::ChannelBroken);
    }
    return mChannel->_tryRecv();
}
template <typename T>
inline auto Receiver<T>::operator =(Receiver &&r) -> Receiver & {
    if (this != &r) {
        close();
        mChannel = r.mChannel;
        r.mChannel = nullptr;
    }
    return *this;
}
template <typename T>
inline auto Receiver<T>::operator =(std::nullptr_t) -> Receiver & {
    close();
    return *this;
}
template <typename T>
inline Receiver<T>::operator bool() const noexcept {
    return mChannel != nullptr;
}

template <typename T>
inline auto Channel<T>::make(size_t size) -> std::pair<Sender<T>, Receiver<T> > {
    auto c = new Channel<T>;
    c->mCapicity = size;
    return std::make_pair(Sender<T>(*c), Receiver<T>(*c));
}
template <typename T>
inline auto Channel<T>::_closeSender() -> void {
    mSenderCount -= 1;
    // Look for sth waiting on it
    if (mReceiverHandle && _isBroken()) {
        auto h = mReceiverHandle;
        mReceiverHandle = nullptr;
        h.resume();
    }
    mRefcount -= 1;
    if (mRefcount == 0) {
        delete this;
    }
}
template <typename T>
inline auto Channel<T>::_closeReceiver() -> void {
    mReceiverClosed = true;
    // Wakeup all sender
    while (!mSenderHandles.empty()) {
        auto h = mSenderHandles.front();
        mSenderHandles.pop_front();
        h.resume();
    }
    mRefcount -= 1;
    if (mRefcount == 0) {
        delete this;
    }
}
template <typename T>
inline auto Channel<T>::_isBroken() const -> bool {
    ILIAS_ASSERT(mRefcount > 0);
    return mSenderCount == 0 || mReceiverClosed;
}
template <typename T>
inline auto Channel<T>::_isEmpty() const -> bool {
    ILIAS_ASSERT(mRefcount > 0);
    return mQueue.empty();
}
template <typename T>
inline auto Channel<T>::_isFull() const -> bool {
    ILIAS_ASSERT(mRefcount > 0);
    return mQueue.size() >= mCapicity;
}
template <typename T>
inline auto Channel<T>::_send(Result<T> &&v) -> Task<> {
    struct Awaiter {
        auto await_ready() const -> bool { return false; }
        auto await_suspend(typename Task<>::handle_type h) -> bool {
            handle = h;
            auto &promise = handle.promise();
            if (promise.isCanceled()) {
                return false;
            }
            // Check if full
            if (!self->_isFull() || self->_isBroken()) {
                return false;
            }
            // Is Full, we try to resume the receiver in the event loop
            if (self->mReceiverHandle) {
                promise.eventLoop()->resumeHandle(self->mReceiverHandle);
                self->mReceiverHandle = nullptr;
            }
            // We add our handle to the waiting list
            self->mSenderHandles.emplace_back(handle);
            added = true;
            return true;
        }
        auto await_resume() -> Result<> {
            if (self->_isBroken()) {
                return Unexpected(Error::ChannelBroken);
            }
            // Adding this value to the queue
            self->mQueue.emplace(std::move(*value));
            // Try resume the waiting list
            if (self->mReceiverHandle) {
                handle.promise().eventLoop()->resumeHandle(self->mReceiverHandle);
                self->mReceiverHandle = nullptr;
            }
            // Check if we are canceled
            if (added) {
                auto it = std::find(self->mSenderHandles.begin(), self->mSenderHandles.end(), handle);
                if (it != self->mSenderHandles.end()) {
                    self->mSenderHandles.erase(it);
                }
            }
            if (handle.promise().isCanceled()) {
                // handle
                return Unexpected(Error::Canceled);
            }
            return Result<void>();
        }
        typename Task<>::handle_type handle;
        bool added = false; //< Did we add our handle to waiting list?
        Result<T> *value = nullptr;
        Channel<T> *self = nullptr;
    };
    Awaiter awaiter;
    awaiter.value = &v;
    awaiter.self = this;
    co_return co_await awaiter;
}
template <typename T>
inline auto Channel<T>::_trySend(Result<T> &&v) -> Result<void> {
    if (_isBroken()) {
        return Unexpected(Error::ChannelBroken);
    }
    if (_isFull()) {
        return Unexpected(Error::ChannelFull);
    }
    mQueue.emplace(std::move(v));
    if (mReceiverHandle) {
        auto h = mReceiverHandle;
        mReceiverHandle = nullptr;
        h.resume();
    }
}
template <typename T>
inline auto Channel<T>::_recv() -> Task<T> {
    struct Awaiter {
        auto await_ready() const -> bool { return false; }
        auto await_suspend(typename Task<T>::handle_type h) -> bool {
            handle = h;
            auto &promise = handle.promise();
            if (promise.isCanceled()) {
                return false;
            }
            if (self->_isBroken() || !self->_isEmpty()) {
                return false;
            }
            self->mReceiverHandle = handle;
            return true;
        }
        auto await_resume() -> Result<T> {
            if (self->mReceiverHandle == handle) {
                self->mReceiverHandle = nullptr;
            }
            if (handle.promise().isCanceled()) {
                return Unexpected(Error::Canceled);
            }
            if (self->mQueue.empty() && self->_isBroken()) {
                return Unexpected(Error::ChannelBroken);
            }
            ILIAS_ASSERT(!self->mQueue.empty());
            // Got value here
            auto val = std::move(self->mQueue.front());
            self->mQueue.pop();

            // Check for sender waiting on it
            if (!self->mSenderHandles.empty()) {
                auto loop = handle.promise().eventLoop();
                loop->resumeHandle(self->mSenderHandles.front());
                self->mSenderHandles.pop_front();
            }
            return val;
        }
        typename Task<T>::handle_type handle;
        Result<T> *value = nullptr;
        Channel<T> *self = nullptr;
    };
    Awaiter awaiter;
    awaiter.self = this;
    co_return co_await awaiter;
}
template <typename T>
inline auto Channel<T>::_tryRecv() -> Result<T> {
    if (_isBroken()) {
        return Unexpected(Error::ChannelBroken);
    }
    if (_isEmpty()) {
        return Unexpected(Error::ChannelEmpty);
    }
    auto value = std::move(mQueue.front());
    mQueue.pop();
    if (!mSenderHandles.empty()) {
        auto h = mSenderHandles.front();
        mSenderHandles.pop_front();
        h.resume();
    }
    return value;
}

ILIAS_NS_END