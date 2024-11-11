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

#include <ilias/sync/detail/channel.hpp>
#include <ilias/task/task.hpp>
#include <concepts>
#include <limits>

ILIAS_NS_BEGIN

namespace mpsc {

template <typename T>
class Channel final : public channel_impl::Channel<
    T, 
    channel_impl::MultiQueue, 
    channel_impl::SingleQueue> 
{
    
};

/**
 * @brief The mpsc sender class. This class is used to send data to the channel. (copy & move able)
 * 
 * @tparam T The item type to be sent.
 */
template <typename T>
class Sender final : public channel_impl::Sender<Channel<T> > {
public:
    using channel_impl::Sender<Channel<T> >::Sender;
};

/**
 * @brief The mpsc receiver class. This class is used to receive data from the channel. (only moveable)
 * 
 * @tparam T The item type to be received.
 */
template <typename T>
class Receiver final : public channel_impl::Receiver<Channel<T> > {
    using channel_impl::Receiver<Channel<T> >::Receiver;
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
 * @param capacity The capacity of the channel. (default to size_t max)
 * @return Pair<T> 
 */
template <std::movable T>
inline auto channel(size_t capacity = std::numeric_limits<size_t>::max()) -> Pair<T> {
    auto ptr = new Channel<T>(capacity);
    return {
        Sender<T>(ptr),
        Receiver<T>(ptr)
    };
}

} // namespace mpsc

ILIAS_NS_END