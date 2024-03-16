#pragma once

#include <condition_variable>
#include <chrono>
#include <thread>
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

    void requestStop() override;
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
    std::condition_variable mTimerCond;
    std::mutex              mTimerMutex;
    std::thread             mTimerThread;
    std::atomic_bool        mTimerQuit {false};
    std::atomic_uintptr_t   mTimerIdBase {0};
};

// --- MiniEventLoop Impl
inline MiniEventLoop::MiniEventLoop() {
    mTimerThread = std::thread(&MiniEventLoop::_timerRun, this);
}
inline MiniEventLoop::~MiniEventLoop() {
    mTimerQuit = true;
    mTimerCond.notify_all();
    mTimerThread.join();
}

inline void MiniEventLoop::requestStop() {
    post([](void *self) {
        static_cast<MiniEventLoop *>(self)->mQuit = true;
    }, this);
}
inline void MiniEventLoop::run() {
    while (!mQuit) {
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, [this]() {
            return !mQueue.empty() || mQuit;
        });
        if (mQuit) {
            break;
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
    std::unique_lock lock(mTimerMutex);
    uintptr_t id = mTimerIdBase.fetch_add(1) + 1;
    time_point expireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

    auto iter = mTimers.insert(std::pair(expireTime, Timer{id, ms, flags, fn, arg}));
    mTimersMap.insert(std::pair(id, iter));
    mTimerCond.notify_one();
    return id;
}
inline bool MiniEventLoop::delTimer(uintptr_t timer) {
    if (timer == 0) {
        return false;
    }
    std::unique_lock lock(mTimerMutex);
    auto iter = mTimersMap.find(timer);
    if (iter == mTimersMap.end()) {
        return false;
    }
    mTimers.erase(iter->second);
    mTimersMap.erase(iter);
    return true;
}

inline void MiniEventLoop::_timerRun() {
    std::unique_lock lock(mTimerMutex);
    while (!mTimerQuit) {
        // Wait for add timer
        if (mTimers.empty()) {
            mTimerCond.wait(lock);
            if (mTimerQuit) {
                break;
            }
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        // Invoke expired timers
        for (auto iter = mTimers.begin(); iter != mTimers.end();) {
            auto [expireTime, timer] = *iter;
            if (expireTime > now) {
                break;
            }
            // Invoke
            post(timer.fn, timer.arg);

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

        if (!mTimers.empty()) {
            // Wait
            mTimerCond.wait_until(lock, mTimers.begin()->first);
        }
    }
}


#ifdef _WIN32

/**
 * @brief A Windows event loop
 * 
 */
class WinEventLoop final : public EventLoop {
public:
    WinEventLoop();
    WinEventLoop(const WinEventLoop &) = delete;
    ~WinEventLoop();

    void requestStop() override;
    void run() override;
    void post(void (*fn)(void *), void *arg = nullptr) override;
    bool delTimer(uintptr_t timer) override;
    uintptr_t addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) override;
private:
    LRESULT _wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    struct Timer {
        HANDLE handle = nullptr;
        void (*fn)(void *) = nullptr;
        void *arg = nullptr;
        EventLoop *eventLoop = nullptr;
        int flags = 0;
    };

    HWND mHwnd = nullptr;
    bool mQuit = false;
};

inline WinEventLoop::WinEventLoop() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        ::WNDCLASSEXW wx {};
        wx.cbSize = sizeof(wx);
        wx.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto self = reinterpret_cast<WinEventLoop *>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
            if (self) {
                return self->_wndproc(hwnd, msg, wParam, lParam);
            }
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        };
        wx.hInstance = ::GetModuleHandleW(nullptr);
        wx.lpszClassName = L"IliasEventLoop";
        ::RegisterClassExW(&wx);
    });
    mHwnd = ::CreateWindowExW(
        0, 
        L"IliasEventLoop",
        L"IliasEventLoop",
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        HWND_MESSAGE,
        nullptr,
        ::GetModuleHandleW(nullptr),
        nullptr
    );
    if (mHwnd == nullptr) {
        return;
    }
    ::SetWindowLongPtrW(mHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}
inline WinEventLoop::~WinEventLoop() {
    ::DestroyWindow(mHwnd);
}
inline void WinEventLoop::run() {
    ::MSG msg;
    while (!mQuit) {
        if (::GetMessageW(&msg, nullptr, 0, 0)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    mQuit = false;
}
inline void WinEventLoop::requestStop() {
    post([](void *self) {
        reinterpret_cast<WinEventLoop *>(self)->mQuit = true;
    }, this);
}
inline void WinEventLoop::post(void (*fn)(void *), void *arg) {
    ::PostMessageW(mHwnd, WM_APP, reinterpret_cast<WPARAM>(fn), reinterpret_cast<LPARAM>(arg));
}
inline uintptr_t WinEventLoop::addTimer(int64_t ms, void (*fn)(void *), void *arg, int flags) {
    auto timer = new Timer {
        nullptr, fn, arg, this, flags
    };
    auto cb = [](void *ptr, BOOLEAN) {
        auto timer = static_cast<Timer*>(ptr);
        timer->eventLoop->post([](void *ptr) {
            auto timer = static_cast<Timer*>(ptr);
            timer->fn(timer->arg);
            if (timer->flags & TimerFlags::TimerSingleShot) {
                if (!::DeleteTimerQueueTimer(nullptr, timer->handle, nullptr)) {
                    auto err = ::GetLastError();
                    if (err != ERROR_IO_PENDING) {
                        printf("Error at DeleteTimerQueueTimer %d\n", int(err));
                    }
                }
                delete timer;
            }
        }, ptr);
    };
    if (!::CreateTimerQueueTimer(&timer->handle, nullptr, cb, timer, ms, 0, WT_EXECUTEONLYONCE)) {
        printf("Error at CreateTimerQueueTimer %d\n", int(::GetLastError()));
        delete timer;
        return 0;
    }
    return reinterpret_cast<uintptr_t>(timer);
}
inline bool WinEventLoop::delTimer(uintptr_t timer) {
    if (timer == 0) {
        return false;
    }
    auto ptr = reinterpret_cast<Timer*>(timer);
    ::DeleteTimerQueueTimer(nullptr, ptr->handle, nullptr);
    delete ptr;
    return true;
}
inline LRESULT WinEventLoop::_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP) {
        auto cb = reinterpret_cast<void(*)(void*)>(wParam);
        auto arg = reinterpret_cast<void*>(lParam);
        if (cb) {
            cb(arg);
        }
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

#endif

using NativeEventLoop = ILIAS_NATIVE_LOOP;

ILIAS_NS_END