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

auto win32::pipe(HANDLE *read, HANDLE *write, SECURITY_ATTRIBUTES *attr) -> bool {
    // MSDN says anymous pipe does not support overlapped I/O, so we have to create a named pipe.
    static constinit std::atomic<int> counter{0};
    wchar_t name[256] {0};
    ::swprintf(
        name, 
        sizeof(name), 
        L"\\\\.\\Pipe\\IliasPipe_%d_%d_%d",
        counter.fetch_add(1),
        int(::time(nullptr)),
        int(::GetCurrentThreadId())
    );

    HANDLE readPipe = ::CreateNamedPipeW(
        name,
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1, // < Only one instance of the pipe, for our write pipe
        65535, //< 64KB buffer size, as same as linux
        65535,
        NMPWAIT_USE_DEFAULT_WAIT,
        attr
    );

    if (readPipe == INVALID_HANDLE_VALUE) {
        *read = INVALID_HANDLE_VALUE;
        *write = INVALID_HANDLE_VALUE;
        return false;
    }

    HANDLE writePipe = ::CreateFileW(
        name,
        GENERIC_WRITE,
        0,
        attr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (writePipe == INVALID_HANDLE_VALUE) {
        ::CloseHandle(readPipe);
        *read = INVALID_HANDLE_VALUE;
        *write = INVALID_HANDLE_VALUE;
        return false;
    }

    *read = readPipe;
    *write = writePipe;
    return true;
}

namespace {
    static constinit std::once_flag once;
    static constinit struct {
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