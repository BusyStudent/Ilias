/**
 * @file mpmc.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The multi producer, multi consumer channel.
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

namespace mpmc {

template <typename T>
using ChannelBase = channel_impl::Channel<
    T,
    channel_impl::MultiQueue,
    channel_impl::MultiQueue
>;

template <typename T>
class Channel final : public ChannelBase<T> {
public:
    Channel(size_t capacity) : ChannelBase<T>(capacity) { }
};

template <typename T>
class Sender final : public channel_impl::Sender<Channel<T> > {
public:
    using channel_impl::Sender<Channel<T> >::Sender;
};

template <typename T>
class Receiver final : public channel_impl::Receiver<Channel<T> > {
public:
    using channel_impl::Receiver<Channel<T> >::Receiver;
};

template <typename T>
struct Pair {
    Sender<T> sender;
    Receiver<T> receiver;
};

template <typename T>
inline auto channel(size_t capacity = std::numeric_limits<size_t>::max()) -> Pair<T> {
    auto ptr = new Channel<T>();
    return {
        Sender<T>(ptr), 
        Receiver<T>(ptr)
    };
}

} // namespace mpmc

ILIAS_NS_END