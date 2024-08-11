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

ILIAS_NS_BEGIN

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
     * @return ::addrinfo* 
     */
    auto operator ->() const -> ::addrinfo *;
    auto operator  *() const -> ::addrinfo &;
    auto operator  =(AddressInfo &&info) -> AddressInfo &;

    /**
     * @brief Try get the address info by it
     * 
     * @param name 
     * @param family 
     * @return Result<AddressInfo> 
     */
    static auto fromHostname(const char *name, int family = AF_UNSPEC) -> Result<AddressInfo>;
private:
    struct FreeInfo {
        auto operator ()(::addrinfo *info) const noexcept -> void {
            ::freeaddrinfo(info);
        }
    };
    std::unique_ptr<::addrinfo, FreeInfo> mInfo;
};

// --- AddressInfo Impl
inline AddressInfo::AddressInfo() = default;
inline AddressInfo::AddressInfo(AddressInfo &&other) = default;
inline AddressInfo::~AddressInfo() = default;

inline auto AddressInfo::operator =(AddressInfo &&info) -> AddressInfo & = default;
inline auto AddressInfo::operator *() const -> ::addrinfo & { return *mInfo; }
inline auto AddressInfo::operator ->() const -> ::addrinfo * { return mInfo.get(); }

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
    ::addrinfo hints {
        .ai_family = family,
    };
    ::addrinfo *info = nullptr;
    if (auto err = ::getaddrinfo(hostname, nullptr, &hints, &info); err != 0) {

#ifdef EAI_SYSTEM
        if (err == EAI_SYSTEM) {
            return Unexpected(Error::fromErrno());
        }
#endif

        return Unexpected(Error(err, GaiCategory::instance()));
    }
    AddressInfo result;
    result.mInfo.reset(info);
    return result;
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
            default: return other == Error::Unknown;
        }
    }
    return false;
}

ILIAS_NS_END