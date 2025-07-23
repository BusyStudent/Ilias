#include <ilias/net/addrinfo.hpp>
#include <ilias/net/system.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/error.hpp>
#include <latch>

#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
    #include <VersionHelpers.h>
#endif

ILIAS_NS_BEGIN


auto GaiCategory::name() const noexcept -> const char * {
    return "getaddrinfo";
}

auto GaiCategory::message(int code) const -> std::string {
    
#if defined(_WIN32) // In windows, the getaddrinfo return Win32 error codes
    return SystemError(code).toString();
#else
    return ::gai_strerror(code);
#endif

}

auto GaiCategory::instance() -> const GaiCategory & {
    static const GaiCategory instance;
    return instance;
}

auto AddressInfo::fromHostnameBlocking(std::string_view name, std::string_view service, std::optional<addrinfo_t> hints) -> IoResult<AddressInfo> {
    addrinfo_t *info = nullptr;
    int err = 0;

#if defined(_WIN32)
    auto wname = win32::toWide(name);
    auto wservice = win32::toWide(service);
    err = ::GetAddrInfoExW(
        wname.c_str(), 
        wservice.c_str(), 
        NS_ALL, 
        nullptr, 
        hints ? &hints.value() : nullptr, 
        &info, 
        nullptr, 
        nullptr, 
        nullptr, 
        nullptr
    );
#else
    err = ::getaddrinfo(name, service, hints ? &hints.value() : nullptr, &info);
#endif

    if (err != 0) {

#ifdef EAI_SYSTEM
        if (err == EAI_SYSTEM) {
            return Err(SystemError::fromErrno());
        }
#endif

        return Err(GaiError(err));
    }
    return AddressInfo(info);

}

auto AddressInfo::fromHostname(std::string_view name, std::string_view service, std::optional<addrinfo_t> hints) -> IoTask<AddressInfo> {

#if defined(_WIN32)
    struct Awaiter : public OVERLAPPED {
        auto await_ready() -> bool {
            auto callback = [](DWORD dwError, DWORD dwBytes, OVERLAPPED *_self) {
                auto self = static_cast<Awaiter *>(_self); // Post back to the executor thread
                self->suspend.wait();
                self->handle.executor().post(onComplete, self);
            };

            ::memset(static_cast<OVERLAPPED *>(this), 0, sizeof(OVERLAPPED));
            err = ::GetAddrInfoExW(
                name.c_str(), 
                service.c_str(), 
                NS_ALL, 
                nullptr, 
                hints ? &hints.value() : nullptr, 
                &info,
                nullptr, 
                this, 
                callback, 
                &namedHandle
            );
            return err != ERROR_IO_PENDING;
        }

        auto await_suspend(runtime::CoroHandle caller) {
            handle = caller;
            reg = runtime::StopRegistration(caller.stopToken(), [this]() {
                ::GetAddrInfoExCancel(&namedHandle);
            });
            suspend.count_down(); // We are now suspended
        }

        auto await_resume() -> IoResult<AddressInfo> {
            if (err != 0) {
                return Err(GaiError(err));
            }
            return AddressInfo(info);
        }

        static auto onComplete(void *_self) -> void {
            auto self = static_cast<Awaiter *>(_self);
            self->err = ::GetAddrInfoExOverlappedResult(self);
            if (self->err == ERROR_OPERATION_ABORTED) {
                self->handle.setStopped();
                return;                
            }
            self->handle.resume();
        }

        // Args
        std::wstring name;
        std::wstring service;
        std::optional<addrinfo_t> hints;

        // State
        runtime::CoroHandle handle;
        runtime::StopRegistration reg;
        std::latch suspend {1};
        HANDLE namedHandle = nullptr;
        addrinfo_t *info = nullptr;
        int err = 0;
    };

    if (::IsWindows8OrGreater()) { // We can use GetAddrInfoExW Async
        Awaiter awaiter;
        awaiter.name = win32::toWide(name);
        awaiter.service = win32::toWide(service);
        awaiter.hints = hints;
        co_return co_await awaiter;
    }

#elif defined(__linux) && defined(__GLIBC__) //< GNU Linux with glibc, use getaddrinfo_a
    ::gaicb request {
        .ar_name = name,
        .ar_service = service,
        .ar_request = hints ? &hints.value() : nullptr
    };
    struct Awaiter {
        auto await_ready() const noexcept { return false; }

        auto await_suspend(TaskView<> caller) -> bool {
            ::memset(&mEvent, 0, sizeof(mEvent));
            mCaller = caller;
            mEvent.sigev_notify = SIGEV_THREAD; //< Use callback
            mEvent.sigev_value.sival_ptr = this;
            mEvent.sigev_notify_function = [](::sigval val) {
                auto self = static_cast<Awaiter *>(val.sival_ptr);
                self->mCaller.schedule();
            };
            int ret = ::getaddrinfo_a(GAI_NOWAIT, &mRequest, 1, &mEvent);
            mReg = caller.cancellationToken().register_([](void *request) {
                auto ret = ::gai_cancel(static_cast<::gaicb*>(request));
            }, mRequest);
            return ret == 0;
        }

        auto await_resume() -> IoResult<AddressInfo> {
            auto err = ::gai_error(mRequest);
            if (err != 0) {
                return Err(Error(err, GaiCategory::instance()));
            }
            return AddressInfo(mRequest->ar_result);
        }

        TaskView<> mCaller;
        ::gaicb *mRequest;
        ::sigevent mEvent;
        CancellationToken::Registration mReg;
    };
    co_return co_await Awaiter { .mRequest = &request };
#endif

    // Fallback to the synchronous version for platforms other than Windows and GNU/Linux.
    // This is because no native asynchronous implementation is available for these platforms.
    // Maybe we start a thread to avoid blocking the current thread. ?
    co_return fromHostnameBlocking(name, service, hints);
}

ILIAS_NS_END