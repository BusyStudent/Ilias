#pragma once

#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
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
    void timerSingleShot(int64_t ms, void (*fn)(void *), void *arg) override;
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
    struct Timer {
        std::chrono::steady_clock::time_point expireTime;
        void (*fn)(void *);
        void *arg;

        // Let thhe heap is min heap
        bool operator <(const Timer &rhs) const {
            return expireTime > rhs.expireTime;
        }
    };
    std::vector<Timer> mTimers;
    std::condition_variable mTimerCond;
    std::mutex              mTimerMutex;
    std::thread             mTimerThread;
    std::atomic_bool        mTimerQuit {false};
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
    std::unique_lock<std::mutex> lock(mMutex);
    mQueue.push_back({fn, arg});
    mCond.notify_one();
}
inline void MiniEventLoop::timerSingleShot(int64_t ms, void (*fn)(void *), void *arg) {
    std::unique_lock<std::mutex> lock(mTimerMutex);
    mTimers.emplace_back(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms), fn, arg
    );
    std::push_heap(mTimers.begin(), mTimers.end());
    mTimerCond.notify_one();
}

inline void MiniEventLoop::_timerRun() {
    std::unique_lock<std::mutex> lock(mTimerMutex);
    std::make_heap(mTimers.begin(), mTimers.end());
    while (!mTimerQuit) {
        // Wait for add timer
        if (mTimers.empty()) {
            mTimerCond.wait(lock);
            if (mTimerQuit) {
                break;
            }
            continue;
        }
        auto first = mTimers.front();
        auto now = std::chrono::steady_clock::now();
        if (now > first.expireTime) {
            std::pop_heap(mTimers.begin(), mTimers.end());
            mTimers.pop_back();

            // Invoke it
            post(first.fn, first.arg);
        }
        if (!mTimers.empty()) {
            // Wait
            mTimerCond.wait_until(lock, mTimers.front().expireTime);
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
    void timerSingleShot(int64_t ms, void (*fn)(void *), void *arg) override;
private:
    LRESULT _wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
inline void WinEventLoop::timerSingleShot(int64_t ms, void (*fn)(void *), void *arg) {
    struct Timer {
        HANDLE handle = nullptr;
        void (*fn)(void *) = nullptr;
        void *arg = nullptr;
        EventLoop *eventLoop = nullptr;
    };
    auto timer = new Timer {
        nullptr, fn, arg, this
    };
    auto cb = [](void *ptr, BOOLEAN) {
        auto timer = static_cast<Timer*>(ptr);
        timer->eventLoop->post([](void *ptr) {
            auto timer = static_cast<Timer*>(ptr);
            if (!::DeleteTimerQueueTimer(nullptr, timer->handle, nullptr)) {
                auto err = ::GetLastError();
                if (err != ERROR_IO_PENDING) {
                    printf("Error at DeleteTimerQueueTimer\n");
                }
            }
            timer->fn(timer->arg);
            delete timer;
        }, ptr);
    };
    if (!::CreateTimerQueueTimer(&timer->handle, nullptr, cb, timer, ms, 0, WT_EXECUTEONLYONCE)) {
        printf("Error at CreateTimerQueueTimer\n");
        delete timer;
    }
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