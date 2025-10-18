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

class [[nodiscard]] EventAwaiter : public WaitAwaiter<EventAwaiter> {
public:
    EventAwaiter(Event &event);

    auto await_ready() const -> bool;
    auto await_resume() -> void;
    auto onWakeup() -> bool;
private:
    Event &mEvent;
friend class Event;
};

} // namespace sync

/**
 * @brief The Coroutine Event object for sync, it is thread safe (manual clear mode, default)
 * 
 * @note Don't use the ```AutoClear``` mode in multi-thread environment, it may cause the wakup lose
 */
class Event {
public:
    enum Flag : uint32_t {
        None      = 0,
        AutoClear = 1, //< Auto clear the event when set, (not thread safe)
    };

    Event() = default;
    Event(Flag flag) : mAutoClear(flag & AutoClear) { }
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
        mIsSet.store(true, std::memory_order_release);
        mQueue.wakeupAll();
        if (mAutoClear) { // Clear it
            mIsSet.store(false, std::memory_order_release);
        }
    }

    /**
     * @brief Set the Auto Clear Attribute
     * @note This operation is not atomatic, you should set before use the Event
     * 
     * @param autoClear true on auto clear, false on manual clear
     */
    auto setAutoClear(bool autoClear) -> void {
        mAutoClear = autoClear;
    }

    /**
     * @brief Check the event is set ?
     * 
     * @return true 
     * @return false 
     */
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
     * @brief Block the current thread to wait the event to be set
     * 
     * @note It will ```BLOCK``` the current thread
     */
    [[nodiscard]]
    auto blockingWait() noexcept {
        return mQueue.blockingWait([&]() {
            return isSet();
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

inline auto sync::EventAwaiter::await_ready() const -> bool {
    return mEvent.isSet();
}

inline auto sync::EventAwaiter::await_resume() -> void {

}

inline auto sync::EventAwaiter::onWakeup() -> bool {
    return mEvent.isSet();
}

ILIAS_NS_END