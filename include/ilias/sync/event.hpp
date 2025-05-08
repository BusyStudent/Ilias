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

#include <ilias/cancellation_token.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <list>

ILIAS_NS_BEGIN

class Event;

namespace detail {

/**
 * @brief The awaiter for waiting event to be set
 * 
 */
class EventAwaiter {
public:
    EventAwaiter(Event &event) : mEvent(event) { }

    auto await_ready() const -> bool;
    auto await_suspend(CoroHandle) -> void;
    auto await_resume() -> Result<void>;

    auto notifySet() -> void;
private:
    auto onCancel() -> void;

    Event &mEvent;
    CoroHandle mCaller;
    bool mCanceled = false; //< Is Canceled ?
    CancellationToken::Registration mReg;
    std::list<EventAwaiter *>::iterator mIt;
};

} // namespace detail

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
    ~Event() {
        ILIAS_ASSERT(mAwaiters.empty());
    }

    /**
     * @brief Clear thr event, make it to the unset
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
        if (mIsSet) {
            return;
        }
        for (const auto &awaiter : mAwaiters) {
            awaiter->notifySet();
        }
        mAwaiters.clear();
        mIsSet = !mAutoClear; // If auto clear, we don't set it
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
     * @brief Waiting the event to be set
     * 
     * @return co_await 
     */
    auto operator co_await() noexcept {
        return detail::EventAwaiter {*this};
    }

    /**
     * @brief Check the event is set ?
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mIsSet;
    }
private:
    std::list<detail::EventAwaiter *> mAwaiters;
    bool mIsSet = false;
    bool mAutoClear = false;
friend detail::EventAwaiter;
};

inline auto detail::EventAwaiter::await_ready() const -> bool {
    return mEvent.isSet();
}

inline auto detail::EventAwaiter::await_suspend(CoroHandle caller) -> void {
    mCaller = caller;
    mEvent.mAwaiters.push_back(this);
    mIt = mEvent.mAwaiters.end();
    --mIt;
    mReg = mCaller.cancellationToken().register_([this]() { onCancel(); });
}

inline auto detail::EventAwaiter::await_resume() -> Result<void> {
    if (mCanceled) {
        return Unexpected(Error::Canceled);
    }
    return {};
}

inline auto detail::EventAwaiter::onCancel() -> void {
   if (mIt == mEvent.mAwaiters.end()) {
        ILIAS_TRACE("Event", "Already got notify, cancel no-op");
        return;
   }
   mEvent.mAwaiters.erase(mIt);
   mIt = mEvent.mAwaiters.end();
   mCanceled = true;
   mCaller.schedule();
}

inline auto detail::EventAwaiter::notifySet() -> void {
    mIt = mEvent.mAwaiters.end();
    mCaller.schedule();
}

ILIAS_NS_END