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
private:
    Event &mEvent;
friend class Event;
};

} // namespace sync

/**
 * @brief The Coroutine Event object for sync, it is not thread safe
 * 
 */
class Event {
public:
    enum Flag : uint32_t {
        None      = 0,
        AutoClear = 1, //< Auto clear the event when set
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
        mIsSet = false;
    }

    /**
     * @brief Set the event, awake the task wating on it
     * 
     */
    auto set() -> void {
        mQueue.wakeupAll();
        mIsSet = !mAutoClear;
    }

    /**
     * @brief Set the Auto Clear Attribute
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
    auto isSet() const -> bool { return mIsSet; }
    
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
     * @brief Waiting the event to be set
     * 
     * @return co_await 
     */
    auto operator co_await() noexcept {
        return sync::EventAwaiter {*this};
    }
private:
    sync::WaitQueue mQueue;
    bool mIsSet = false;
    bool mAutoClear = false;
friend sync::EventAwaiter;
};

inline sync::EventAwaiter::EventAwaiter(Event &event) : WaitAwaiter(event.mQueue), mEvent(event) {

}

inline auto sync::EventAwaiter::await_ready() const -> bool {
    return mEvent.isSet();
}

inline auto sync::EventAwaiter::await_resume() -> void {

}

ILIAS_NS_END