#pragma once

#include <condition_variable>
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
private:
    struct Fn {
        void (*fn)(void *);  //< Callback function
        void *arg;            //< Argument for callback function
    };
    std::deque<Fn> mQueue;
    std::condition_variable mCond;
    std::mutex              mMutex;
    bool  mQuit = false;
};

// --- MiniEventLoop Impl
inline MiniEventLoop::MiniEventLoop() {

}
inline MiniEventLoop::~MiniEventLoop() {

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