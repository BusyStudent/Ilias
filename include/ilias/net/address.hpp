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

#include <ilias/detail/overloads.hpp>
#include <ilias/net/system.hpp>
#include <ilias/result.hpp>
#include <charconv>
#include <compare>
#include <variant>
#include <array>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief The IPV4 address class
 * 
 */
class IPAddress4 : public ::in_addr {
public:
    constexpr IPAddress4() = default;
    constexpr IPAddress4(::in_addr addr4) : ::in_addr {addr4} {}
    constexpr IPAddress4(const IPAddress4 &other) = default;

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
     * @return std::array<uint8_t, sizeof(::in_addr)> 
     */
    auto toUint8Array() const -> std::array<uint8_t, sizeof(::in_addr)> {
        return std::bit_cast<std::array<uint8_t, sizeof(::in_addr)> >(*this);
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
    static auto fromString(std::string_view value) -> Result<IPAddress4, std::errc> {
        if (value.size() >= INET_ADDRSTRLEN) {
            return Err(std::errc::invalid_argument);
        }

#if 0
        ::in_addr addr;
        ::std::array<char, INET_ADDRSTRLEN> buf {};
        ::memcpy(buf.data(), value.data(), value.size());
        if (auto res = ::inet_pton(AF_INET, buf.data(), &addr); res == 1) {
            return addr;
        }
        return Err(std::errc::invalid_argument);
#else   // Parse on our own, for std::string_view, avoid copy
        ::std::array<uint8_t, sizeof(::in_addr)> array {};
        auto end = value.data() + value.size();
        auto ptr = value.data();
        // Parse xxx.xxx.xxx.xxx
        for (size_t idx = 0; idx < 4; ++idx) {
            auto [cur, ec] = std::from_chars(ptr, end, array[idx]);
            if (ec != std::errc()) {
                return Err(ec);
            }
            if (idx == 3) {
                if (cur != end) { // Must be end of string
                    return Err(std::errc::invalid_argument);                    
                }
                break;
            }
            if (cur == end || *cur != '.') { // End of string or next is not .
                return Err(std::errc::invalid_argument);
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
    constexpr IPAddress6() = default;
    constexpr IPAddress6(::in6_addr addr6) : ::in6_addr {addr6} {}
    constexpr IPAddress6(const IPAddress6 &other) = default;

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
     * @brief Convert ipv6 address to ipv4 (if mapped)
     * 
     * @return Result<IPAddress4, std::errc> 
     */
    auto toV4() const -> Result<IPAddress4, std::errc> {
        if (!isV4Mapped()) {
            return Err(std::errc::invalid_argument);
        }
        ::in_addr addr {};
        ::memcpy(&addr, &s6_addr[12], sizeof(addr));
        return addr;
    }

    /**
     * @brief Cast self to a byte span
     * 
     * @return std::span<const std::byte> 
     */
    auto span() const -> std::span<const std::byte> {
        return std::as_bytes(std::span {this, 1});
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
     * @brief Check this ipv6 address is v4 mapped
     * 
     * @return true 
     * @return false 
     */
    auto isV4Mapped() const -> bool {
        return IN6_IS_ADDR_V4MAPPED(this);
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
     * @brief Create ipv6 address from mapping ipv4 address
     * 
     * @param addr The ipv4 address
     * @return IPAddress6 
     */
    static auto fromV4Mapped(IPAddress4 addr) -> IPAddress6 {
        ::in6_addr v6 {};
        v6.s6_addr[10] = 0xff;
        v6.s6_addr[11] = 0xff;
        ::memcpy(v6.s6_addr + 12, &addr, sizeof(addr));
        return v6;
    }

    /**
     * @brief Copy data from buffer to create ipv6 address
     * 
     * @param mem pointer to network-format ipv6 address
     * @param n must be sizeof(::in6_addr)
     * @return IPAddress6 
     */
    static auto fromRaw(const void *mem, [[maybe_unused]] size_t n) -> IPAddress6 {
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
    static auto fromString(std::string_view value) -> Result<IPAddress6, std::errc> {
        if (value.size() >= INET6_ADDRSTRLEN) {
            return Err(std::errc::invalid_argument);
        }

#if 1
        ::in6_addr addr;
        ::std::array<char, INET6_ADDRSTRLEN> buf {};
        ::memcpy(buf.data(), value.data(), value.size());
        if (auto res = ::inet_pton(AF_INET6, buf.data(), &addr); res == 1) {
            return addr;
        }
        return Err(std::errc::invalid_argument);
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
    constexpr IPAddress() = default;
    constexpr IPAddress(::in_addr addr) : mData {addr} {}
    constexpr IPAddress(::in6_addr addr) : mData {addr} {}
    constexpr IPAddress(const IPAddress &other) = default;

    /**
     * @brief Construct a new IPAddress object
     * 
     * @param str The IPV4 or IPV6 address string, if failed, the family will be AF_UNSPEC
     */
    template <typename T> requires (std::convertible_to<T, std::string_view>)
    IPAddress(const T &str) : IPAddress{fromString(str).value_or(IPAddress {})} {}

    /**
     * @brief Convert the address to string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        constexpr auto visitor = Overloads {
            [](std::monostate) { return std::string {}; },
            [](const IPAddress4 &addr) { return addr.toString(); },
            [](const IPAddress6 &addr) { return addr.toString(); },
        };
        return std::visit(visitor, mData);
    }

    /**
     * @brief Cast self to a byte span 
     * 
     * @return std::span<const std::byte> 
     */
    auto span() const -> std::span<const std::byte> {
        constexpr auto visitor = Overloads {
            [](std::monostate) { return std::span<const std::byte> {}; },
            [](const IPAddress4 &addr) { return addr.span(); },
            [](const IPAddress6 &addr) { return addr.span(); },
        };
        return std::visit(visitor, mData);
    }

    /**
     * @brief Get the address family
     * 
     * @return int 
     */
    auto family() const -> int {
        constexpr auto visitor = Overloads {
            [](std::monostate) { return AF_UNSPEC; },
            [](const IPAddress4 &addr) { return AF_INET; },
            [](const IPAddress6 &addr) { return AF_INET6; },
        };
        return std::visit(visitor, mData);
    }

    /**
     * @brief Get the address length in bytes (0 on invalid)
     * 
     * @return size_t 
     */
    auto length() const -> size_t {
        switch (family()) {
            case AF_INET: return sizeof(::in_addr);
            case AF_INET6: return sizeof(::in6_addr);
            default: return 0;
        }
    }

    /**
     * @brief Check if the address is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool {
        return !std::holds_alternative<std::monostate>(mData);
    }

    /**
     * @brief Cast the address to specific type
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto cast() const -> const T & {
        return std::get<T>(mData);
    }

    /**
     * @brief Cast the address to specific type
     * 
     * @tparam T 
     * @return T& 
     */
    template <typename T>
    auto cast() -> T & {
        return std::get<T>(mData);
    }

    /**
     * @brief Compare this address with other
     * 
     */
    auto operator <=>(const IPAddress &other) const noexcept = default;

    /**
     * @brief Try parse the IP address from string
     * 
     * @param str The IPV4 or IPV6 address string
     * @return Result<IPAddress>, if has value, the family will be AF_INET or AF_INET6
     */
    static auto fromString(std::string_view str) -> Result<IPAddress, std::errc> {
        if (str.size() > INET6_ADDRSTRLEN) {
            return Err(std::errc::invalid_argument);
        }

        if (str.find(':') != std::string_view::npos) {
            auto res = IPAddress6::fromString(str);
            if (!res) {
                return Err(res.error());
            }
            return *res;
        }
        else {
            auto res = IPAddress4::fromString(str);
            if (!res) {
                return Err(res.error());
            }
            return *res;
        }
    }

    /**
     * @brief Try get the IP address from raw data
     * 
     * @param data 
     * @param length 
     * @return Result<IPAddress> 
     */
    static auto fromRaw(const void *data, size_t length) -> Result<IPAddress, std::errc> {
        switch (length) {
            case sizeof(::in_addr): return *reinterpret_cast<const ::in_addr *>(data);
            case sizeof(::in6_addr): return *reinterpret_cast<const ::in6_addr *>(data);
            default: return Err(std::errc::invalid_argument);
        }
    }

    /**
     * @brief Check if the address is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return isValid();
    }
private:
    std::variant<
        std::monostate,
        IPAddress4,
        IPAddress6
    > mData;
};

ILIAS_NS_END

// Interop with std
template <>
struct std::hash<ilias::IPAddress4> {
    auto operator()(const ilias::IPAddress4 &addr) const noexcept -> size_t {
        return std::hash<uint32_t>{}(addr.toUint32());
    }
};

template <>
struct std::hash<ilias::IPAddress6> {
    auto operator()(const ilias::IPAddress6 &addr) const noexcept -> size_t {
        auto span = addr.span();
        auto view = std::string_view {reinterpret_cast<const char*>(span.data()), span.size()};
        return std::hash<std::string_view>{}(view); // HACKY way to do it :(
    }
};

template <>
struct std::hash<ilias::IPAddress> {
    auto operator()(const ilias::IPAddress &addr) const noexcept -> size_t {
        auto span = addr.span();
        auto view = std::string_view {reinterpret_cast<const char*>(span.data()), span.size()};
        return std::hash<std::string_view>{}(view); // HACKY way to do it :(
    }
};