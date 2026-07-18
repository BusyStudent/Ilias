/**
 * @file channel_core.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provide some core utils (such as lifetime smart pointer) for channel
 * @version 0.1
 * @date 2026-07-17
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/sync/detail/futex.hpp> // FutexMutex
#include <memory>
#include <atomic>

ILIAS_NS_BEGIN

namespace sync {

/**
 * @brief The base class of channel, manage the lifetime of channel
 * 
 */
class ChannelBase {
public:
    constexpr ChannelBase() = default;
    constexpr ChannelBase(const ChannelBase &) = delete;

#if !defined(NDEBUG)
    ~ChannelBase() {
        ILIAS_ASSERT(mRefCount.load() == 0, "ChannelBase is not released correctly?");
    }
#endif

    auto onDeref() -> bool {
        if (mRefCount.fetch_sub(1) == 1) {
            return true;
        }
        return false;
    }
private:
    std::atomic<uint8_t> mRefCount {2}; // 2 for sender and receiver
};

class ChanSenderDeleter {
public:
    template <typename T> requires(std::is_base_of_v<ChannelBase, T>)
    auto operator()(T *p) const -> void {
        p->onSenderClose();
        if (p->onDeref()) {
            delete p;
        }
    }
};

class ChanReceiverDeleter {
public:
    template <typename T> requires(std::is_base_of_v<ChannelBase, T>)
    auto operator()(T *p) const -> void {
        p->onReceiverClose();
        if (p->onDeref()) {
            delete p;
        }
    }
};

template <typename T>
concept Sendable = std::is_move_constructible_v<T> && (!std::is_reference_v<T>);

} // namespace sync

ILIAS_NS_END