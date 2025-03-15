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

#include <ilias/net/system.hpp>
#include <ilias/error.hpp>
#include <charconv>
#include <compare>
#include <array>
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
        return networkToHost(s_addr);
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
     * @brief Convert the address to uint8_t array in network order
     * 
     * @return std::array<uint8_t, 4> 
     */
    auto toUint8Array() const -> std::array<uint8_t, 4> {
        return std::bit_cast<std::array<uint8_t, 4> >(*this);
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
        return toUint8Array() <=> addr.toUint8Array();
    }

    /**
     * @brief Compare two ipv4 addresses
     * 
     */
    auto operator ==(const IPAddress4 &addr) const {
        return toUint8Array() == addr.toUint8Array();
    }


    /**
     * @brief Compare two ipv4 addresses
     * 
     */
    auto operator !=(const IPAddress4 &addr) const {
        return toUint8Array() != addr.toUint8Array();
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
        return fromUint32NetworkOrder(hostToNetwork(value));
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
     * @brief Create ipv4 address from uint8 array, network order
     * 
     * @param array 
     * @return IPAddress4 
     */
    static auto fromUint8Array(std::array<uint8_t, sizeof(::in_addr)> array) -> IPAddress4 {
        return std::bit_cast<IPAddress4>(array);
    }

    /**
     * @brief Try to convert a string to an IPV4 address
     * 
     * @param str 
     * @return Result<IPAddress4> 
     */
    static auto fromString(std::string_view value) -> Result<IPAddress4> {
        if (value.size() >= INET_ADDRSTRLEN) {
            return Unexpected(Error::InvalidArgument);
        }

#if 0
        ::in_addr addr;
        ::std::array<char, INET_ADDRSTRLEN> buf {0};
        ::memcpy(buf.data(), value.data(), value.size());
        if (auto res = ::inet_pton(AF_INET, buf.data(), &addr); res == 1) {
            return addr;
        }
        return Unexpected(Error::InvalidArgument);
#else   // Parse on our own, for std::string_view, avoid copy
        ::std::array<uint8_t, sizeof(::in_addr)> array;
        auto end = value.data() + value.size();
        auto ptr = value.data();
        // Parse xxx.xxx.xxx.xxx
        for (size_t idx = 0; idx < 4; ++idx) {
            auto [cur, ec] = std::from_chars(ptr, end, array[idx]);
            if (ec != std::errc()) {
                return Unexpected(Error::InvalidArgument);
            }
            if (idx == 3) {
                if (cur != end) { // Must be end of string
                    return Unexpected(Error::InvalidArgument);                    
                }
                break;
            }
            if (cur == end || *cur != '.') { // End of string or next is not .
                return Unexpected(Error::InvalidArgument);
            }
            ptr = cur + 1; // Skip .
        }
        return std::bit_cast<IPAddress4>(array);
#endif
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
     * @brief Convert ipv6 address to uint8 array (uint8 * 16) in network order 
     * 
     * @return std::array<uint8_t, 16>
     */
    auto toUint8Array() const -> std::array<uint8_t, 16> {
        return std::bit_cast<std::array<uint8_t, 16> >(*this);
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
    auto operator <=>(const IPAddress6 &other) const {
        return toUint8Array() <=> other.toUint8Array();
    }

    /**
     * @brief Compare this ipv6 address with other
     * 
     */
    auto operator ==(const IPAddress6 &other) const {
        return toUint8Array() == other.toUint8Array();
    }

    /**
     * @brief Compare this ipv6 address with other
     * 
     */
    auto operator !=(const IPAddress6 &other) const {
        return toUint8Array() != other.toUint8Array();
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
     * @brief Copy data from buffer to create ipv6 address
     * 
     * @param mem pointer to network-format ipv6 address
     * @param n must be sizeof(::in6_addr)
     * @return IPAddress6 
     */
    static auto fromRaw(const void *mem, size_t n) -> IPAddress6 {
        return *reinterpret_cast<const ::in6_addr *>(mem);
    }

    /**
     * @brief Create ipv6 address from uint8 array (uint8 * 16) in network order
     * 
     * @param arr 
     * @return IPAddress6 
     */
    static auto fromUint8Array(std::array<uint8_t, 16> arr) -> IPAddress6 {
        return std::bit_cast<IPAddress6>(arr);
    }

    /**
     * @brief Parse the ipv6 address from string
     * 
     * @param value 
     * @return IPAddress6 
     */
    static auto fromString(std::string_view value) -> Result<IPAddress6> {
        if (value.size() >= INET6_ADDRSTRLEN) {
            return Unexpected(Error::InvalidArgument);
        }

#if 1
        ::in6_addr addr;
        ::std::array<char, INET6_ADDRSTRLEN> buf {0};
        ::memcpy(buf.data(), value.data(), value.size());
        if (auto res = ::inet_pton(AF_INET6, buf.data(), &addr); res == 1) {
            return addr;
        }
        return Unexpected(Error::InvalidArgument);
#endif
        // TODO: Parse ipv6 address on own, avoid copy string
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
     * @brief Construct a new IPAddress object
     * 
     * @param str The IPV4 or IPV6 address string, if failed, the family will be AF_UNSPEC
     */
    IPAddress(std::string_view str) : IPAddress(fromString(str).value_or(IPAddress{ })) { }

    /**
     * @brief Construct a new IPAddress object by string
     * 
     * @param str The IPV4 or IPV6 address string, if failed, the family will be AF_UNSPEC
     */
    IPAddress(const std::string &str) : IPAddress(fromString(str).value_or(IPAddress{ })) { }

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
        if (mFamily != AF_UNSPEC) {
            return ::memcmp(&mAddr, &other.mAddr, length()) == 0;
        }
        return true; // Both are invalid
    }

    /**
     * @brief Compare this address with other
     * 
     */
    auto operator !=(const IPAddress &other) const -> bool {
        return !(*this == other);
    }

    /**
     * @brief Compare this address with other
     * 
     */
    auto operator <=>(const IPAddress &other) const -> std::strong_ordering {
        if (mFamily != other.mFamily) {
            return mFamily <=> other.mFamily;
        }
        if (mFamily == AF_UNSPEC) { // All invalid
            return std::strong_ordering::equal;
        }
        return ::memcmp(&mAddr, &other.mAddr, length()) <=> 0;
    }

    /**
     * @brief Try parse the IP address from string
     * 
     * @param str The IPV4 or IPV6 address string
     * @return Result<IPAddress>, if has value, the family will be AF_INET or AF_INET6
     */
    static auto fromString(std::string_view str) -> Result<IPAddress> {
        if (str.size() > INET6_ADDRSTRLEN) {
            return Unexpected(Error::InvalidArgument);
        }

#if 0
        int family = AF_INET; //< Default try to parse IPV4 address
        if (str.find(':') != std::string_view::npos) {
            family = AF_INET6;
        }
        IPAddress ret;
        ::std::array<char, INET6_ADDRSTRLEN> buf {0};
        ::memcpy(buf.data(), str.data(), str.size());
        if (auto res = ::inet_pton(family, buf.data(), &ret.mAddr); res == 1) {
            ret.mFamily = family;
            return ret;
        }
        return Unexpected(Error::InvalidArgument);
#else
        if (str.find(':') != std::string_view::npos) {
            auto res = IPAddress6::fromString(str);
            if (!res) {
                return Unexpected(res.error());
            }
            return res.value();
        }
        else {
            auto res = IPAddress4::fromString(str);
            if (!res) {
                return Unexpected(res.error());
            }
            return res.value();
        }
#endif

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