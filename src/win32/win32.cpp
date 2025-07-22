#include <ilias/detail/win32defs.hpp>
#include <mutex> // std::once_flag

ILIAS_NS_BEGIN

auto win32::toWide(std::string_view s) -> std::wstring {
    int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), nullptr, 0);
    if (len <= 0) {
        return {};
    }

    std::wstring wstr(len, 0);
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), wstr.data(), len);
    return wstr;
}

auto win32::toUtf8(std::wstring_view s) -> std::string {
    int len = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), s.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }

    std::string str(len, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), s.size(), str.data(), len, nullptr, nullptr);
    return str;
}

namespace {
    static std::once_flag once;
    static struct {
        decltype(::SetThreadDescription) *SetThreadDescription = nullptr;
        decltype(::GetThreadDescription) *GetThreadDescription = nullptr;
    } apis;

    auto init() {
        auto kernel = ::GetModuleHandleW(L"kernel32.dll");
        if (kernel) {
            apis.SetThreadDescription = reinterpret_cast<decltype(apis.SetThreadDescription)>(::GetProcAddress(kernel, "SetThreadDescription"));
            apis.GetThreadDescription = reinterpret_cast<decltype(apis.GetThreadDescription)>(::GetProcAddress(kernel, "GetThreadDescription"));
        }
    }
}

auto win32::setThreadName(std::string_view name) -> bool {
    std::call_once(once, init);
    if (apis.SetThreadDescription) {
        return SUCCEEDED(apis.SetThreadDescription(::GetCurrentThread(), toWide(name).c_str()));
    }
    return false;
}

auto win32::threadName() -> std::string {
    std::call_once(once, init);
    LPWSTR name = nullptr;
    if (apis.GetThreadDescription && SUCCEEDED(apis.GetThreadDescription(::GetCurrentThread(), &name))) {
        auto str = toUtf8(name);
        ::LocalFree(name);
        return str;
    }
    return {};
}

ILIAS_NS_END