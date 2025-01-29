/**
 * @file addrinfo.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For wrapping getaddrinfo and getnameinfo
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/net/endpoint.hpp>
#include <ilias/net/address.hpp>
#include <ilias/net/system.hpp>
#include <ilias/task/task.hpp>
#include <optional>

#if defined(_WIN32)
    #define ILIAS_ADDRINFO ::ADDRINFOEXW
    #include <ilias/detail/win32.hpp> //< EventOverlapped
    #include <VersionHelpers.h>
#else
    #include <csignal>
    #define ILIAS_ADDRINFO ::addrinfo
#endif

ILIAS_NS_BEGIN

/**
 * @brief The platform specific addrinfo
 * 
 */
using addrinfo_t = ILIAS_ADDRINFO;

/**
 * @brief Error for getaddrinfo and getnameinfo
 * 
 */
class GaiCategory final : public ErrorCategory {
public:
    auto name() const -> std::string_view override;
    auto message(int64_t code) const -> std::string override;
    auto equivalent(int64_t self, const Error &other) const -> bool override;

    static auto instance() -> GaiCategory &;
};

/**
 * @brief Wrapper for addrinfo
 * 
 */
class AddressInfo {
public:
    AddressInfo();
    AddressInfo(const AddressInfo &) = delete;
    AddressInfo(AddressInfo &&info);
    ~AddressInfo();

    /**
     * @brief Get all addresses from the info
     * 
     * @return std::vector<IPAddress> 
     */
    auto addresses() const -> std::vector<IPAddress>;

    /**
     * @brief Get all endpoint from the info
     * 
     * @return std::vector<IPEndpoint> 
     */
    auto endpoints() const -> std::vector<IPEndpoint>;

    /**
     * @brief Access the wrapped addrinfo
     * 
     * @return addrinfo_t* 
     */
    auto operator ->() const -> addrinfo_t *;
    auto operator  *() const -> addrinfo_t &;
    auto operator  =(AddressInfo &&info) -> AddressInfo &;

    /**
     * @brief Try get the address info by it
     * 
     * @param name The hostname string
     * @param family 
     * @return Result<AddressInfo> 
     */
    static auto fromHostname(const char *name, int family = AF_UNSPEC) -> Result<AddressInfo>;

    /**
     * @brief Try get the address info by it asynchronously
     * 
     * @param name The hostname string
     * @param family 
     * @return IoTask<AddressInfo> 
     */
    static auto fromHostnameAsync(const char *name, int family = AF_UNSPEC) -> IoTask<AddressInfo>;

    /**
     * @brief Wrapping the raw getaddrinfo
     * 
     * @param name The hostname string
     * @param service 
     * @param hint 
     * @return Result<AddressInfo> 
     */
    static auto fromHostname(const char *name, const char *service, std::optional<addrinfo_t> hints = {}) -> Result<AddressInfo>;

    /**
     * @brief Wrapping the raw getaddrinfo asynchronously
     * 
     * @param name The hostname string
     * @param service 
     * @param hints 
     * @return IoTask<AddressInfo> 
     */
    static auto fromHostnameAsync(const char *name, const char *service, std::optional<addrinfo_t> hints = {}) -> IoTask<AddressInfo>;
private:
    AddressInfo(addrinfo_t *info) : mInfo(info) { }

    struct FreeInfo {
        auto operator ()(addrinfo_t *info) const noexcept -> void {
#if defined(_WIN32)
            ::FreeAddrInfoExW(info);
#else
            ::freeaddrinfo(info);
#endif
        }
    };
    std::unique_ptr<addrinfo_t, FreeInfo> mInfo;
};

// --- AddressInfo Impl
inline AddressInfo::AddressInfo() = default;
inline AddressInfo::AddressInfo(AddressInfo &&other) = default;
inline AddressInfo::~AddressInfo() = default;

inline auto AddressInfo::operator =(AddressInfo &&info) -> AddressInfo & = default;
inline auto AddressInfo::operator *() const -> addrinfo_t & { return *mInfo; }
inline auto AddressInfo::operator ->() const -> addrinfo_t * { return mInfo.get(); }

inline auto AddressInfo::addresses() const -> std::vector<IPAddress> {
    std::vector<IPAddress> vec;
    for (auto cur = mInfo.get(); cur != nullptr; cur = cur->ai_next) {
        auto ep = IPEndpoint::fromRaw(cur->ai_addr, cur->ai_addrlen);
        if (ep) {
            vec.emplace_back(ep->address());
        }
    }
    return vec;
}

inline auto AddressInfo::endpoints() const -> std::vector<IPEndpoint> {
    std::vector<IPEndpoint> vec;
    for (auto cur = mInfo.get(); cur != nullptr; cur = cur->ai_next) {
        auto ep = IPEndpoint::fromRaw(cur->ai_addr, cur->ai_addrlen);
        if (ep) {
            vec.emplace_back(ep.value());
        }
    }
    return vec;
}

inline auto AddressInfo::fromHostname(const char *hostname, int family) -> Result<AddressInfo> {
    addrinfo_t hints {
        .ai_family = family,
    };
    return fromHostname(hostname, nullptr, hints);
}


inline auto AddressInfo::fromHostnameAsync(const char *hostname, int family) -> IoTask<AddressInfo> {
    addrinfo_t hints {
        .ai_family = family,
    };
    return fromHostnameAsync(hostname, nullptr, hints);
}

inline auto AddressInfo::fromHostname(const char *name, const char *service, std::optional<addrinfo_t> hints) -> Result<AddressInfo> {
    addrinfo_t *info = nullptr;
    int err = 0;

#if defined(_WIN32)
    err = ::GetAddrInfoExW(
        name ? win32::toWide(name).c_str() : nullptr, 
        service ? win32::toWide(service).c_str() : nullptr, 
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
            return Unexpected(SystemError::fromErrno());
        }
#endif

        return Unexpected(Error(err, GaiCategory::instance()));
    }
    return AddressInfo(info);

}

inline auto AddressInfo::fromHostnameAsync(const char *name, const char *service, std::optional<addrinfo_t> hints) -> IoTask<AddressInfo> {
    addrinfo_t *info = nullptr;
    int err = 0;

#if defined(_WIN32)
    if (!::IsWindows8OrGreater()) {
        co_return fromHostname(name, service, hints); //< fallback to sync version
    }
    win32::EventOverlapped overlapped;
    HANDLE namedHandle = nullptr;
    err = ::GetAddrInfoExW(
        name ? win32::toWide(name).c_str() : nullptr, 
        service ? win32::toWide(service).c_str() : nullptr, 
        NS_ALL, 
        nullptr, 
        hints ? &hints.value() : nullptr, 
        &info,
        nullptr, 
        &overlapped, 
        nullptr, 
        &namedHandle
    );
    if (err == ERROR_SUCCESS) { // Done
        co_return AddressInfo(info); 
    }
    if (err != ERROR_IO_PENDING) { //< FAILED to start overlapped
        co_return Unexpected(Error(err, GaiCategory::instance()));
    }
    if (auto ret = co_await overlapped; !ret) {
        ::GetAddrInfoExCancel(&namedHandle);
        co_return Unexpected(ret.error());
    }
    err = ::GetAddrInfoExOverlappedResult(&overlapped);
    if (err != 0) {
        co_return Unexpected(Error(err, GaiCategory::instance()));
    }
    co_return AddressInfo(info);
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

        auto await_resume() -> Result<AddressInfo> {
            auto err = ::gai_error(mRequest);
            if (err != 0) {
                return Unexpected(Error(err, GaiCategory::instance()));
            }
            return AddressInfo(mRequest->ar_result);
        }

        TaskView<> mCaller;
        ::gaicb *mRequest;
        ::sigevent mEvent;
        CancellationToken::Registration mReg;
    };
    co_return co_await Awaiter { .mRequest = &request };
#else
    co_return fromHostname(name, service, hints); //< fallback to sync version
#endif
}

// GaiCategory
inline auto GaiCategory::instance() -> GaiCategory & { 
    static GaiCategory c; 
    return c; 
}

inline auto GaiCategory::name() const -> std::string_view { 
    return "getaddrinfo"; 
}

inline auto GaiCategory::message(int64_t code) const -> std::string {

#ifdef _WIN32
    // In Windows, the gai error is windows error
    return Error(SystemError(code)).message();
#else
    return ::gai_strerror(code);
#endif

}

inline auto GaiCategory::equivalent(int64_t code, const Error &other) const -> bool {
    if (this == &other.category() && code == other.value()) {
        //< Category is same, value is same
        return true;
    }
    if (other.category() == IliasCategory::instance()) {
        switch (code) {
            case EAI_NONAME: return other == Error::HostNotFound;

            //< For async version, it is extension of the platform getaddrinfo
#if defined(ERROR_OPERATION_ABORTED)
            case ERROR_OPERATION_ABORTED: return other == Error::Canceled;
#endif

#if defined(ECANCELED) && !defined(_WIN32) // For POSIX Cancelation
            case ECANCELED: return other == Error::Canceled;
#endif
            default: return other == Error::Unknown;
        }
    }
    return false;
}

ILIAS_NS_END