#pragma once

#include <condition_variable>
#include <chrono>
#include <deque>
#include <mutex>
#include <map>
#include "ilias_co.hpp"

#ifndef ILIAS_NATIVE_LOOP
    #define ILIAS_NATIVE_LOOP MiniEventLoop
#endif

#ifdef _WIN32
    #undef ILIAS_NATIVE_LOOP
    #define ILIAS_NATIVE_LOOP WinEventLoop
    #include <Windows.h>

    #pragma comment(lib, "user32.lib")
#endif

ILIAS_NS_BEGIN

/**
 * @brief A Mini event loop
 * 
 */
class MiniEventLoop final : public EventLoop {
public:
    MiniEventLoop();
    MiniEventLoop(const MiniEventLoop &) = delete;
    ~MiniEventLoop();

    void quit() override;
    void run() override;
    void post(void (*fn)(void *), void *arg) override;
    bool delTimer(uintptr_t timer) override;
    uintptr_t addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) override;
private:
    void _timerRun();

    struct Fn {
        void (*fn)(void *);  //< Callback function
        void *arg;            //< Argument for callback function
    };
    std::deque<Fn> mQueue;
    std::condition_variable mCond;
    std::mutex              mMutex;
    bool  mQuit = false;

    // Timer
    using time_point = std::chrono::steady_clock::time_point;
    struct Timer {
        uintptr_t id; //< TimerId
        int64_t ms;   //< Interval in milliseconds
        int flags;    //< Timer flags
        void (*fn)(void *);
        void *arg;
    };
    std::map<uintptr_t, std::multimap<time_point, Timer>::iterator> mTimersMap; //< Mapping id to iterator
    std::multimap<time_point, Timer> mTimers; //< Sort by expireTime
    std::atomic_uintptr_t   mTimerIdBase {0};
};

// --- MiniEventLoop Impl
inline MiniEventLoop::MiniEventLoop() {

}
inline MiniEventLoop::~MiniEventLoop() {

}

inline void MiniEventLoop::quit() {
    post([](void *self) {
        static_cast<MiniEventLoop *>(self)->mQuit = true;
    }, this);
}
inline void MiniEventLoop::run() {
    while (!mQuit) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mQueue.empty()) {
            if (!mTimers.empty()) {
                // Wait
                auto expireTime = mTimers.begin()->first;
                mCond.wait_until(lock, expireTime);
            }
            else {
                // Wait new event
                mCond.wait(lock);
            }
        }
        if (mQuit) {
            break;
        }
        _timerRun();
        if (mQueue.empty()) {
            continue;
        }
        auto fn = mQueue.front();
        mQueue.pop_front();
        lock.unlock();
        fn.fn(fn.arg);
    }
    mQuit = false; //< Restore
}
inline void MiniEventLoop::post(void (*fn)(void *), void *arg) {
    std::unique_lock lock(mMutex);
    mQueue.push_back({fn, arg});
    mCond.notify_one();
}
inline uintptr_t MiniEventLoop::addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) {
    uintptr_t id = mTimerIdBase.fetch_add(1) + 1;
    time_point expireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

    auto iter = mTimers.insert(std::pair(expireTime, Timer{id, ms, flags, fn, arg}));
    mTimersMap.insert(std::pair(id, iter));
    return id;
}
inline bool MiniEventLoop::delTimer(uintptr_t timer) {
    if (timer == 0) {
        return false;
    }
    auto iter = mTimersMap.find(timer);
    if (iter == mTimersMap.end()) {
        return false;
    }
    mTimers.erase(iter->second);
    mTimersMap.erase(iter);
    return true;
}

inline void MiniEventLoop::_timerRun() {
    auto now = std::chrono::steady_clock::now();
    // Invoke expired timers
    for (auto iter = mTimers.begin(); iter != mTimers.end();) {
        auto [expireTime, timer] = *iter;
        if (expireTime > now) {
            break;
        }
        // Invoke
        mQueue.push_back({timer.fn, timer.arg});

        // Cleanup if
        if (timer.flags & TimerFlags::TimerSingleShot) {
            mTimersMap.erase(timer.id); // Remove the timer
        }
        else {
            auto newExpireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(timer.ms);
            auto newIter = mTimers.insert(iter, std::make_pair(newExpireTime, timer));
            mTimersMap[timer.id] = newIter;
        }
        iter = mTimers.erase(iter); // Move next
    }
}

ILIAS_NS_END