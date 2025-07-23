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
#include <ilias/net/system.hpp> // for
#include <ilias/task/task.hpp> // for Task
#include <ilias/io/error.hpp> // for IoResult
#include <optional>

#if defined(_WIN32)
    #define ILIAS_ADDRINFO ::ADDRINFOEXW
#else
    #define ILIAS_ADDRINFO ::addrinfo
    #include <csignal>
#endif

ILIAS_NS_BEGIN

/**
 * @brief The platform specific addrinfo
 * 
 */
using addrinfo_t = ILIAS_ADDRINFO;

/**
 * @brief The raw error code of getaddrinfo and getnameinfo
 * 
 */
enum class GaiError : int {
    TryAgain                  = EAI_AGAIN,
    Fail                      = EAI_FAIL,
    OutOfMemory               = EAI_MEMORY,
    NotFound                  = EAI_NONAME,
    AddressFamilyNotSupported = EAI_FAMILY,
};

/**
 * @brief Error for getaddrinfo and getnameinfo
 * 
 */
class ILIAS_API GaiCategory final : public std::error_category {
public:
    auto name() const noexcept -> const char* override;
    auto message(int value) const -> std::string override;

    static auto instance() -> const GaiCategory &;
};

ILIAS_DECLARE_ERROR(GaiError, GaiCategory);

/**
 * @brief Wrapper for addrinfo
 * 
 */
class ILIAS_API AddressInfo {
public:
    struct iterator {
        auto operator ++() noexcept -> iterator & { ptr = ptr->ai_next; return *this; }
        auto operator *() -> IPEndpoint { return IPEndpoint::fromRaw(ptr->ai_addr, ptr->ai_addrlen).value(); }
        auto operator <=>(const iterator &other) const noexcept = default;
        addrinfo_t *ptr = nullptr;
    };

    explicit AddressInfo(addrinfo_t *info) noexcept : mInfo(info) { }
    AddressInfo() = default;
    AddressInfo(const AddressInfo &) = delete;
    AddressInfo(AddressInfo &&info) = default;

    /**
     * @brief Get all endpoint from the info
     * 
     * @return std::vector<IPEndpoint> 
     */
    auto endpoints() const -> std::vector<IPEndpoint>;

    /**
     * @brief Get the canonical name of the info
     * 
     * @return std::string 
     */
    auto canonicalName() const -> std::string;

    /**
     * @brief Access the wrapped addrinfo
     * 
     * @return addrinfo_t* 
     */
    auto get() const -> addrinfo_t *;
    auto end() const -> iterator;
    auto begin() const -> iterator;
    auto operator  =(AddressInfo &&info) -> AddressInfo &;

    /**
     * @brief Check if the info is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept;

    /**
     * @brief Try get the address info by it asynchronously
     * 
     * @param name The hostname string
     * @param family 
     * @return IoTask<AddressInfo> 
     */
    static auto fromHostname(std::string_view name, int family = AF_UNSPEC) -> IoTask<AddressInfo>;

    /**
     * @brief Try get the address info by it
     * 
     * @param name The hostname string
     * @param family 
     * @return IoResult<AddressInfo> 
     */
    static auto fromHostnameBlocking(std::string_view name, int family = AF_UNSPEC) -> IoResult<AddressInfo>;

    /**
     * @brief Wrapping the raw getaddrinfo asynchronously
     * 
     * @param name The hostname string
     * @param service 
     * @param hints 
     * @return IoTask<AddressInfo> 
     */
    static auto fromHostname(std::string_view name, std::string_view service, std::optional<addrinfo_t> hints = {}) -> IoTask<AddressInfo>;

    /**
     * @brief Wrapping the raw getaddrinfo
     * 
     * @param name The hostname string
     * @param service 
     * @param hint 
     * @return IoResult<AddressInfo> 
     */
    static auto fromHostnameBlocking(std::string_view name, std::string_view service, std::optional<addrinfo_t> hints = {}) -> IoResult<AddressInfo>;
private:
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
inline AddressInfo::operator bool() const noexcept { return mInfo != nullptr; }
inline auto AddressInfo::operator =(AddressInfo &&info) -> AddressInfo & = default;
inline auto AddressInfo::get() const -> addrinfo_t * { return mInfo.get(); }
inline auto AddressInfo::begin() const -> iterator { return {mInfo.get()}; }
inline auto AddressInfo::end() const -> iterator { return {nullptr}; }

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

inline auto AddressInfo::canonicalName() const -> std::string {
    if (!mInfo->ai_canonname) {
        return {};
    }
#if defined(_WIN32)
    return win32::toUtf8(mInfo->ai_canonname);
#else
    return mInfo->ai_canonname;
#endif // _WIN32
}

inline auto AddressInfo::fromHostnameBlocking(std::string_view hostname, int family) -> IoResult<AddressInfo> {
    addrinfo_t hints {
        .ai_family = family,
    };
    return fromHostnameBlocking(hostname, {}, hints);
}


inline auto AddressInfo::fromHostname(std::string_view hostname, int family) -> IoTask<AddressInfo> {
    addrinfo_t hints {
        .ai_family = family,
    };
    return fromHostname(hostname, {}, hints);
}

ILIAS_NS_END