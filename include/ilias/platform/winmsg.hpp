#pragma once

#include <ilias/detail/win32defs.hpp>
#include <ilias/platform/delegate.hpp>
#include <ilias/platform/iocp.hpp>
#include <ilias/io/system_error.hpp> // SystemError
#include <mutex> // std::call_once, std::mutex

#if defined(_MSC_VER)
    #pragma comment(lib, "user32.lib")
#endif // _MSC_VER

ILIAS_NS_BEGIN

namespace win32 {

/**
 * @brief The IoContext run on the message loop of the current thread. it delegate the io to another thread's io context.
 * 
 */
class WinMsgContext final : public DelegateContext<IocpContext> {
public:
    WinMsgContext();
    WinMsgContext(const WinMsgContext &) = delete;
    ~WinMsgContext();

    auto post(void (*fn)(void *), void *args) -> void override;
    auto run(runtime::StopToken token) -> void override;
private:
    auto wndproc(UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT;
    static auto CALLBACK wndprocProxy(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT;

    HWND mHwnd = nullptr;
};

inline WinMsgContext::WinMsgContext() {
    static constinit std::once_flag flag;
    std::call_once(flag, []() {
        ::WNDCLASSEXW wc {
            .cbSize = sizeof(wc),
            .lpfnWndProc = wndprocProxy,
            .lpszClassName = L"IliasWinMsgContext",
        };
        ::RegisterClassExW(&wc);
    });
    mHwnd = ::CreateWindowExW(
        0,
        L"IliasWinMsgContext",
        nullptr,
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE, // We only process the message
        nullptr,
        ::GetModuleHandleW(nullptr),
        this
    );
    if (mHwnd == nullptr) {
        ILIAS_THROW(std::system_error(SystemError::fromErrno(), "CreateWindowExW failed"));
    }
    ::SetWindowLongPtrW(mHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

inline WinMsgContext::~WinMsgContext() {
    if (mHwnd) {
        ::DestroyWindow(mHwnd);
    }
}

inline auto WinMsgContext::post(void (*fn)(void *), void *args) -> void {
    ILIAS_ASSERT(fn);
    ::PostMessageW(mHwnd, WM_USER, reinterpret_cast<WPARAM>(fn), reinterpret_cast<LPARAM>(args));
}

inline auto WinMsgContext::run(runtime::StopToken token) -> void {
    bool running = true;
    runtime::StopCallback callback(token, [&]() {
        schedule([&]() {
            running = false;
        });
    });
    ::MSG msg {};
    while (running && ::GetMessageW(&msg, mHwnd, 0, 0)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

inline auto WinMsgContext::wndproc(UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
    switch (msg) {
        case WM_USER: {
            auto fn = reinterpret_cast<void (*)(void *)>(wParam);
            auto args = reinterpret_cast<void *>(lParam);
            ILIAS_ASSERT(fn);
            fn(args);
            return 0;
        }
    }
    return ::DefWindowProcW(mHwnd, msg, wParam, lParam);
}

inline auto WinMsgContext::wndprocProxy(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
    auto self = reinterpret_cast<WinMsgContext *>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        return self->wndproc(msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace win32

// Re-export this to user
using win32::WinMsgContext;

ILIAS_NS_END