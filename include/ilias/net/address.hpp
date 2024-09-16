/**
 * @file address.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For wrapping in_addr and in6_addr
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#include <ilias/detail/expected.hpp>
#include <ilias/net/system.hpp>
#include <ilias/error.hpp>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief The IPV4 address class
 * 
 */
class IPAddress4 : public ::in_addr {
public:
    IPAddress4() = default;
    IPAddress4(::in_addr addr4) : ::in_addr(addr4) { }
    IPAddress4(const IPAddress4 &other) = default;

    /**
     * @brief Convert the address to string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        char buf[INET_ADDRSTRLEN] {0};
        return ::inet_ntop(AF_INET, this, buf, sizeof(buf));
    }

    /**
     * @brief Convert the address to uint32_t
     * 
     * @return uint32_t 
     */
    auto toUint32() const -> uint32_t {
        return ::ntohl(s_addr);
    }

    /**
     * @brief Convert the address to uint32_t in network order
     * 
     * @return uint32_t 
     */
    auto toUint32NetworkOrder() const -> uint32_t {
        return s_addr;
    }

    /**
     * @brief Cast self to a byte span
     * 
     * @return std::span<const std::byte> 
     */
    auto span() const -> std::span<const std::byte> {
        return std::as_bytes(std::span(this, 1));
    }

    /**
     * @brief Check current address is any
     * 
     * @return true 
     * @return false 
     */
    auto isAny() const -> bool {
        return toUint32() == INADDR_ANY;
    }

    /**
     * @brief Check current address is none
     * 
     * @return true 
     * @return false 
     */
    auto isNone() const -> bool {
        return toUint32() == INADDR_NONE;
    }

    /**
     * @brief Check current address is loopback
     * 
     * @return true 
     * @return false 
     */
    auto isLoopback() const -> bool {
        return toUint32() == INADDR_LOOPBACK;
    }

    /**
     * @brief Check current address is broadcast
     * 
     * @return true 
     * @return false 
     */
    auto isBroadcast() const -> bool {
        return toUint32() == INADDR_BROADCAST;
    }

    /**
     * @brief Check current address is multicast
     *
     * @return true 
     * @return false 
     */
    auto isMulticast() const -> bool {
        return IN_MULTICAST(toUint32());
    }

    /**
     * @brief Compare two ipv4 addresses
     * 
     */
    auto operator <=>(const IPAddress4 &addr) const {
        return toUint32() <=> addr.toUint32();
    }

    /**
     * @brief Compare two ipv4 addresses
     * 
     */
    auto operator ==(const IPAddress4 &addr) const {
        return toUint32() == addr.toUint32();
    }


    /**
     * @brief Compare two ipv4 addresses
     * 
     */
    auto operator !=(const IPAddress4 &addr) const {
        return toUint32() != addr.toUint32();
    }

    /**
     * @brief Get any ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto any() -> IPAddress4 {
        return fromUint32(INADDR_ANY);
    }

    /**
     * @brief Get none ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto none() -> IPAddress4 {
        return fromUint32(INADDR_NONE);
    }

    /**
     * @brief Get loop ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto loopback() -> IPAddress4 {
        return fromUint32(INADDR_LOOPBACK);
    }

    /**
     * @brief Get broadcast ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto broadcast() -> IPAddress4 {
        return fromUint32(INADDR_BROADCAST);
    }

    /**
     * @brief Copy data from buffer to create ipv4 address
     * 
     * @param mem pointer to network-format ipv4 address
     * @param n must be sizeof(::in_addr)
     * @return IPAddress4 
     */
    static auto fromRaw(const void *mem, size_t n) -> IPAddress4 {
        ILIAS_ASSERT(n == sizeof(::in_addr));
        return IPAddress4(*reinterpret_cast<const ::in_addr *>(mem));
    }

    /**
     * @brief Create ipv4 address from uint32, host order
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromUint32(uint32_t value)  -> IPAddress4 {
        return fromUint32NetworkOrder(::htonl(value));
    }
    /**
     * @brief Create ipv4 address from uint32, network order
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromUint32NetworkOrder(uint32_t value) -> IPAddress4 {
        IPAddress4 addr;
        addr.s_addr = value;
        return addr;
    }

    /**
     * @brief Try to convert a string to an IPV4 address
     * 
     * @param str 
     * @return Result<IPAddress4> 
     */
    static auto fromString(const char *str) -> Result<IPAddress4> {
        ::in_addr addr;
        if (auto res = ::inet_pton(AF_INET, str, &addr); res == 1) {
            return addr;
        }
        return Unexpected(Error::InvalidArgument);
    }
};

/**
 * @brief The IPV6 address class
 * 
 */
class IPAddress6 : public ::in6_addr {
public:
    IPAddress6() = default;
    IPAddress6(::in6_addr addr6) : ::in6_addr(addr6) { }
    IPAddress6(const IPAddress6 &other) = default;

    /**
     * @brief Convert ipv6 address to human readable string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        char buf[INET6_ADDRSTRLEN] {0};
        return ::inet_ntop(AF_INET6, this, buf, sizeof(buf));
    }

    /**
     * @brief Cast self to a byte span
     * 
     * @return std::span<const std::byte> 
     */
    auto span() const -> std::span<const std::byte> {
        return std::as_bytes(std::span(this, 1));
    }

    /**
     * @brief Check this ipv6 address is any
     * 
     * @return true 
     * @return false 
     */
    auto isAny() const -> bool {
        return IN6_IS_ADDR_UNSPECIFIED(this);
    }

    /**
     * @brief Check this ipv6 address is none
     * 
     * @return true 
     * @return false 
     */
    auto isNone() const -> bool {
        return IN6_IS_ADDR_UNSPECIFIED(this);
    }

    /**
     * @brief Check this ipv6 address is loopback
     * 
     * @return true 
     * @return false 
     */
    auto isLoopback() const -> bool {
        return IN6_IS_ADDR_LOOPBACK(this);
    }

    /**
     * @brief Check this ipv6 address is multicast
     * 
     * @return true 
     * @return false 
     */
    auto isMulticast() const -> bool {
        return IN6_IS_ADDR_MULTICAST(this);
    }

    /**
     * @brief Compare this ipv6 address with other
     * 
     */
    auto operator ==(const IPAddress6 &other) const {
        return ::memcmp(this, &other, sizeof(::in6_addr)) == 0;
    }

    auto operator !=(const IPAddress6 &other) const {
        return ::memcmp(this, &other, sizeof(::in6_addr)) != 0;
    }

    /**
     * @brief Get the any ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto any() -> IPAddress6 {
        return ::in6_addr IN6ADDR_ANY_INIT;
    }

    /**
     * @brief Get the none ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto none() -> IPAddress6 {
        return ::in6_addr IN6ADDR_ANY_INIT;
    }

    /**
     * @brief Get the loop ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto loopback() -> IPAddress6 {
        return ::in6_addr IN6ADDR_LOOPBACK_INIT;
    }

    /**
     * @brief Parse the ipv6 address from string
     * 
     * @param value 
     * @return IPAddress6 
     */
    static auto fromString(const char *value) -> Result<IPAddress6> {
        ::in6_addr addr;
        if (auto res = ::inet_pton(AF_INET6, value, &addr); res == 1) {
            return addr;
        }
        return Unexpected(Error::InvalidArgument);
    }
};

/**
 * @brief The wrapper class for both IPV4 and IPV6 addresses
 * 
 */
class IPAddress {
public:
    IPAddress() = default;
    IPAddress(::in_addr addr) : mFamily(AF_INET) { mAddr.v4 = addr; }
    IPAddress(::in6_addr addr) : mFamily(AF_INET6) { mAddr.v6 = addr; }
    IPAddress(const IPAddress &other) = default;

    /**
     * @brief Construct a new IPAddress object by string
     * 
     * @param str The IPV4 or IPV6 address string, if failed, the family will be AF_UNSPEC
     */
    IPAddress(const char *str) : IPAddress(fromString(str).value_or(IPAddress{ })) { }

    /**
     * @brief Convert the address to string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        char buffer[INET6_ADDRSTRLEN] {0};
        if (mFamily == AF_UNSPEC) {
            return std::string();
        }
        return ::inet_ntop(mFamily, &mAddr, buffer, sizeof(buffer));
    }

    /**
     * @brief Cast self to a byte span 
     * 
     * @return std::span<const std::byte> 
     */
    auto span() const -> std::span<const std::byte> {
        switch (mFamily) {
            case AF_INET: return std::as_bytes(std::span(&mAddr.v4, 1));
            case AF_INET6: return std::as_bytes(std::span(&mAddr.v6, 1));
            default: return {};
        }
    }

    /**
     * @brief Get the address family
     * 
     * @return int 
     */
    auto family() const -> int {
        return mFamily;
    }

    /**
     * @brief Get the address length in bytes (0 on invalid)
     * 
     * @return size_t 
     */
    auto length() const -> size_t {
        switch (mFamily) {
            case AF_INET: return sizeof(::in_addr);
            case AF_INET6: return sizeof(::in6_addr);
            default: return 0;
        }
    }

    auto isValid() const -> bool {
        return mFamily != AF_UNSPEC;
    }

    /**
     * @brief Get the wrapped address data pointer
     * 
     * @return const void* 
     */
    auto data() const -> const void * {
        return &mAddr;
    }

    /**
     * @brief Get the wrapped address data pointer
     * 
     * @return void* 
     */
    auto data() -> void * {
        return &mAddr;
    }

    /**
     * @brief Cast the address to specific type
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto cast() const -> const T & {
        return reinterpret_cast<const T &>(mAddr);
    }

    /**
     * @brief Cast the address to specific type
     * 
     * @tparam T 
     * @return T& 
     */
    template <typename T>
    auto cast() -> T & {
        return reinterpret_cast<T &>(mAddr);
    }
    
    /**
     * @brief Compare this address with other
     * 
     */
    auto operator ==(const IPAddress &other) const -> bool {
        if (mFamily != other.mFamily) {
            return false;
        }
        return ::memcmp(&mAddr, &other.mAddr, length()) == 0;
    }

    /**
     * @brief Compare this address with other
     * 
     */
    auto operator !=(const IPAddress &other) const -> bool {
        return !(*this == other);
    }

    /**
     * @brief Try parse the IP address from string
     * 
     * @param str The IPV4 or IPV6 address string
     * @return Result<IPAddress>, if has value, the family will be AF_INET or AF_INET6
     */
    static auto fromString(const char *str) -> Result<IPAddress> {
        int family = AF_INET; //< Default try to parse IPV4 address
        if (::strchr(str, ':')) {
            family = AF_INET6;
        }
        IPAddress ret;
        if (auto res = ::inet_pton(family, str, &ret.mAddr); res == 1) {
            ret.mFamily = family;
            return ret;
        }
        return Unexpected(Error::InvalidArgument);
    }

    /**
     * @brief Try get the IP address from raw data
     * 
     * @param data 
     * @param length 
     * @return Result<IPAddress> 
     */
    static auto fromRaw(const void *data, size_t length) -> Result<IPAddress> {
        switch (length) {
            case sizeof(::in_addr): return *reinterpret_cast<const ::in_addr *>(data);
            case sizeof(::in6_addr): return *reinterpret_cast<const ::in6_addr *>(data);
            default: return Unexpected(Error::InvalidArgument);
        }
    }
private:
    union {
        ::in_addr  v4;
        ::in6_addr v6;
    } mAddr;
    int mFamily = AF_UNSPEC;
};


ILIAS_NS_END


// --- Formatter for IPAddress4, IPAddress6, IPAddress
#if !defined(ILIAS_NO_FORMAT)
ILIAS_FORMATTER(IPAddress4) {
    auto format(const auto &addr, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", addr.toString());
    }
};

ILIAS_FORMATTER(IPAddress6) {
    auto format(const auto &addr, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", addr.toString());
    }
};

ILIAS_FORMATTER(IPAddress) {
    auto format(const auto &addr, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", addr.toString());
    }
};
#endif