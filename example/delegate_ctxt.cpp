#include <ilias/platform/delegate.hpp>
#include <ilias/platform.hpp>
#include <ilias/http.hpp>
#include <iostream>
#include <Windows.h>

using namespace ILIAS_NAMESPACE;

class WinContext final : public DelegateContext<IocpContext> { //< Delegate the io to the iocp
public:
    WinContext();
    ~WinContext();

    auto post(void (*fn)(void *), void *arg) -> void override;
    auto run(CancellationToken &token) -> void override;
private:
    auto wndproc(UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT;

    HWND mHwnd = nullptr; //< The window handle for dispatching the messages
};

WinContext::WinContext() {
    ::WNDCLASSEXW wc {
        .cbSize = sizeof(::WNDCLASSEXW),
        .lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto self = reinterpret_cast<WinContext *>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self) {
                return self->wndproc(msg, wParam, lParam);
            }
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        },
        .hInstance = ::GetModuleHandleW(nullptr),
        .lpszClassName = L"WinContext"
    };
    if (!::RegisterClassExW(&wc)) {
        throw std::runtime_error("Failed to register window class");
    }
    mHwnd = ::CreateWindowExW(
        0, 
        wc.lpszClassName, 
        L"WinContext", 
        WS_OVERLAPPEDWINDOW, 
        CW_USEDEFAULT, 
        CW_USEDEFAULT, 
        10,
        10, 
        HWND_MESSAGE, //< Only for message
        nullptr, 
        ::GetModuleHandleW(nullptr), 
        nullptr
    );
    if (!mHwnd) {
        throw std::runtime_error("Failed to create window");
    }
    ::SetWindowLongPtrW(mHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

WinContext::~WinContext() {
    ::DestroyWindow(mHwnd);
}

auto WinContext::post(void (*fn)(void *), void *arg) -> void {
    ::PostMessageW(mHwnd, WM_USER, reinterpret_cast<WPARAM>(fn), reinterpret_cast<LPARAM>(arg));
}

auto WinContext::run(CancellationToken &token) -> void {
    BOOL stop = false;
    auto reg = token.register_([&]() {
        stop = true;
        post(nullptr, nullptr); //< Wakeup the message loop
    });
    while (!stop) {
        MSG msg;
        if (::GetMessageW(&msg, nullptr, 0, 0)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
}

auto WinContext::wndproc(UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
    switch (msg) {
        case WM_USER: {
            auto fn = reinterpret_cast<void (*) (void *)>(wParam);
            if (fn) {
                fn(reinterpret_cast<void*>(lParam));
            }
            break;
        }
    }
    return ::DefWindowProcW(mHwnd, msg, wParam, lParam);
}


auto main() -> int {
    WinContext ctxt;
    HttpSession session(ctxt);
    auto fn = [&]() -> Task<void> {
        auto reply = co_await session.get("http://www.baidu.com");
        if (!reply) {
            std::cout << reply.error().toString() << std::endl;
            co_return;
        }
        std::cout << (co_await reply.value().text()).value() << std::endl;
        co_return;
    };
    fn().wait();
    return 0;
}