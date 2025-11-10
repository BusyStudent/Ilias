/**
 * @file event.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The sync event class
 * @version 0.1
 * @date 2024-12-25
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/sync/detail/queue.hpp> // WaitQueue, WaitAwaiter
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <optional> // std::optional

ILIAS_NS_BEGIN

class Event;

namespace sync {

class [[nodiscard]] EventAwaiter final : public WaitAwaiter<EventAwaiter> {
public:
    EventAwaiter(Event &event);

    auto await_resume() -> void;
    auto onWakeup() -> bool;
private:
    Event &mEvent;
friend class Event;
};

} // namespace sync

/**
 * @brief The Coroutine Event object for sync, it is thread safe
 * 
 * @note The ```AutoClear``` mode make the  event clear automatically when waiter wakeup, it is recommended to use for single customer
 */
class Event final {
public:
    enum Flag : uint32_t {
        None      = 0,
        AutoClear = 1, //< Auto clear the event when waiter wakeup (single customer)
    };

    /**
     * @brief Construct a new Event object
     * 
     * @param flag The event flag
     * @param init The initial state of the event
     */
    explicit Event(Flag flag, bool init = false) : mIsSet(init), mAutoClear(flag & AutoClear) { }

    /**
     * @brief Construct a new Event object
     * 
     * @param init The initial state of the event
     */
    explicit Event(bool init = false) : mIsSet(init) { }

    Event(const Event &) = delete;
    ~Event() = default;

    /**
     * @brief Clear the event
     * 
     */
    auto clear() -> void {
        mIsSet.store(false, std::memory_order_release);
    }

    /**
     * @brief Set the event, awake the task wating on it
     * 
     */
    auto set() -> void {
        if (mIsSet.exchange(true, std::memory_order_acq_rel)) { // The prev is true, nothing to do
            return;
        }
        if (mAutoClear) {
            mQueue.wakeupOne();
        }
        else {
            mQueue.wakeupAll();
        }
    }

    /**
     * @brief Check the event is set ?
     * 
     * @return true 
     * @return false 
     */
    [[nodiscard]]
    auto isSet() const -> bool { return mIsSet.load(std::memory_order_acquire); }
    
    /**
     * @brief Wait the event to be set
     * 
     * @return sync::EventAwaiter 
     */
    [[nodiscard]]
    auto wait() noexcept { 
        return sync::EventAwaiter {*this}; 
    }

    /**
     * @brief Try wait the event to be set
     * @note It will clear the event if the event is set (AutoClear mode)
     * 
     * @return bool (true on set, false on not set)
     */
    [[nodiscard]]
    auto tryWait() noexcept {
        if (!mAutoClear) {
            return isSet();
        }
        // We need clear if the event is set
        auto prev = mIsSet.exchange(false, std::memory_order_acq_rel);
        return prev;
    }

    /**
     * @brief Block the current thread to wait the event to be set
     * 
     * @note It will ```BLOCK``` the current thread
     */
    [[nodiscard]]
    auto blockingWait() noexcept {
        return mQueue.blockingWait([&]() {
            return tryWait();
        });
    }

    /**
     * @brief Waiting the event to be set
     * 
     * @return co_await 
     */
    auto operator co_await() noexcept {
        return sync::EventAwaiter {*this};
    }
private:
    sync::WaitQueue   mQueue;
    std::atomic<bool> mIsSet     {false};
    bool              mAutoClear {false};
friend sync::EventAwaiter;
};

inline sync::EventAwaiter::EventAwaiter(Event &event) : WaitAwaiter(event.mQueue), mEvent(event) {

}

inline auto sync::EventAwaiter::await_resume() -> void {

}

inline auto sync::EventAwaiter::onWakeup() -> bool {
    return mEvent.tryWait();
}

ILIAS_NS_END