// INTERNAL !!!
/**
 * @file timer.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provide generic timer implementation, useful when you write IoContext timers
 * @version 0.1
 * @date 2024-08-14
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <optional>  
#include <chrono>
#include <map>

ILIAS_NS_BEGIN

namespace runtime {

class TimerAwaiter;

/**
 * @brief The mini timer implementation
 * 
 */
class TimerService final {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using TimerId = std::multimap<TimePoint, TimerAwaiter *>::iterator;

    TimerService() { }
    TimerService(const TimerService &) = delete;
    ~TimerService() {
        if (!mTimers.empty()) {
            ILIAS_ERROR("TimerService", "There are still {} timers left, memory leak", mTimers.size());
        }
        ILIAS_ASSERT(mTimers.empty());
    }

    /**
     * @brief Process all the timers, submit timeouted tasks to the executor
     * 
     */
    auto updateTimers() -> void;

    /**
     * @brief Get the timepoint to of the next timer
     * 
     * @return std::optional<std::chrono::steady_clock::time_point> 
     */
    auto nextTimepoint() const -> std::optional<TimePoint>;

    /**
     * @brief Sleep for a specified amount of time, make task
     * 
     * @param ms 
     * @return TimerAwaiter 
     */
    auto sleep(uint64_t ms) -> TimerAwaiter;
private:
    /**
     * @brief Submit a timer task to run at a specified timepoint
     * 
     * @param timepoint 
     * @param awaiter 
     * @return The iterator to the timer
     */
    auto submitTimer(TimePoint timepoint, TimerAwaiter *awaiter) -> TimerId;

    /**
     * @brief Cancel a timer task
     * 
     * @param it The iterator to the timer
     * @return auto 
     */
    auto cancelTimer(TimerId timerId) -> void;

    std::multimap<
        TimePoint, 
        TimerAwaiter *
    > mTimers;
friend class TimerAwaiter;
};


/**
 * @brief The timer awaiter, internal use only
 * 
 */
class TimerAwaiter {
public:
    TimerAwaiter(TimerService &service, uint64_t timeout) : mService(service), mTimeout(timeout) { }

    auto await_ready() const -> bool;
    auto await_suspend(CoroHandle caller) -> void;
    auto await_resume() -> void;
private:
    auto onStopped() -> void;
    auto onTimeout() -> void;

    TimerService &mService;
    uint64_t mTimeout;
    
    CoroHandle mCaller;
    StopRegistration mReg;
    std::optional<TimerService::TimerId> mTimerId; //< The timer id
friend class TimerService;
};


inline auto TimerService::nextTimepoint() const -> std::optional<std::chrono::steady_clock::time_point> {
    if (mTimers.empty()) {
        return std::nullopt;
    }
    ILIAS_TRACE("TimerSevice", "Next timepoint is {}", mTimers.begin()->first.time_since_epoch());
    return mTimers.begin()->first;
}

inline auto TimerService::updateTimers() -> void {
    if (mTimers.empty()) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    for (auto iter = mTimers.begin(); iter != mTimers.end(); ) {
        auto &[timepoint, awaiter] = *iter;
        if (timepoint > now) {
            break;
        }
        ILIAS_TRACE("TimerSevice", "Submit timer at {}, diff {}, awaiter {}", timepoint.time_since_epoch(), now - timepoint, (void*) awaiter);
        awaiter->onTimeout();
        iter = mTimers.erase(iter);
    }
}

inline auto TimerService::sleep(uint64_t ms) -> TimerAwaiter {
    return TimerAwaiter {*this, ms};    
}

inline auto TimerService::submitTimer(TimePoint timepoint, TimerAwaiter *awaiter) -> TimerId {
    ILIAS_TRACE("TimerSevice", "Submit timer(on {}, awaiter {})", timepoint.time_since_epoch(), (void *) awaiter);
    return mTimers.emplace(timepoint, awaiter);
}

inline auto TimerService::cancelTimer(TimerId id) -> void {
    ILIAS_TRACE("TimerSevice", "Cancel timer(on {}, awaiter {})", id->first.time_since_epoch(), (void *) id->second);
    mTimers.erase(id);
}


inline auto TimerAwaiter::await_ready() const -> bool {
    return mTimeout == 0;
}

inline auto TimerAwaiter::await_suspend(CoroHandle caller) -> void {
    mCaller = caller;
    mTimerId = mService.submitTimer(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(mTimeout),
        this
    );
    mReg = StopRegistration(caller.stopToken(), [this]() {
        onStopped();
    });
}

inline auto TimerAwaiter::await_resume() -> void {
    ILIAS_ASSERT(!mTimerId.has_value()); // Timer should be canceled or timeout
}

inline auto TimerAwaiter::onStopped() -> void {
    if (!mTimerId) {
        // No-timer id means the timer is already in exector queue
        return;
    }
    mService.cancelTimer(*mTimerId);
    mTimerId = std::nullopt;
    mCaller.setStopped();
}

inline auto TimerAwaiter::onTimeout() -> void {
    mTimerId = std::nullopt;
    mCaller.schedule(); //< Put the caller back to the executor
}

} // namespace runtime

ILIAS_NS_END