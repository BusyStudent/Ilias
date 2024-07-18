#pragma once

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

#include "sys.hpp"
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief Wrapper for v4
 * 
 */
class IPAddress4 : public ::in_addr {
public:
    IPAddress4();
    IPAddress4(::in_addr addr4);
    IPAddress4(const IPAddress4 &other) = default;

    /**
     * @brief Convert to human readable string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    /**
     * @brief Convert to uint32 host order
     * 
     * @return uint32_t 
     */
    auto toUint32() const -> uint32_t;
    /**
     * @brief Convert to uint32 network order
     * 
     * @return uint32_t 
     */
    auto toUint32NetworkOrder() const -> uint32_t;
    /**
     * @brief Get the readonly span of the data
     * 
     * @tparam T 
     * @return std::span<const T> 
     */
    template <typename T = uint8_t>
    auto span() const -> std::span<const T, sizeof(::in_addr) / sizeof(T)>;

    /**
     * @brief Check current address is any
     * 
     * @return true 
     * @return false 
     */
    auto isAny() const -> bool;
    /**
     * @brief Check current address is none
     * 
     * @return true 
     * @return false 
     */
    auto isNone() const -> bool;
    /**
     * @brief Check current address is loopback
     * 
     * @return true 
     * @return false 
     */
    auto isLoopback() const -> bool;
    /**
     * @brief Check current address is broadcast
     * 
     * @return true 
     * @return false 
     */
    auto isBroadcast() const -> bool;
    /**
     * @brief Check current address is multicast
     *
     * @return true 
     * @return false 
     */
    auto isMulticast() const -> bool;

    auto operator ==(const IPAddress4 &) const -> bool;
    auto operator !=(const IPAddress4 &) const -> bool;

    /**
     * @brief Get any ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto any() -> IPAddress4;
    /**
     * @brief Get none ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto none() -> IPAddress4;
    /**
     * @brief Get loop ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto loopback() -> IPAddress4;
    /**
     * @brief Get broadcast ipv4 address
     * 
     * @return IPAddress4 
     */
    static auto broadcast() -> IPAddress4;
    /**
     * @brief Copy data from buffer to create ipv4 address
     * 
     * @param mem pointer to network-format ipv4 address
     * @param n must be sizeof(::in_addr)
     * @return IPAddress4 
     */
    static auto fromRaw(const void *mem, size_t n) -> IPAddress4;
    /**
     * @brief Parse string and create ipv4 address
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromString(const char *value) -> IPAddress4;
    /**
     * @brief Parse the hostname and get ipv4 address
     * 
     * @param hostname 
     * @return IPAddress4 
     */
    static auto fromHostname(const char *hostname) -> IPAddress4;
    /**
     * @brief Create ipv4 address from uint32, host order
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromUint32(uint32_t value)  -> IPAddress4;
    /**
     * @brief Create ipv4 address from uint32, network order
     * 
     * @param value 
     * @return IPAddress4 
     */
    static auto fromUint32NetworkOrder(uint32_t value) -> IPAddress4;
};

/**
 * @brief Wrapper for ipv6 address
 * 
 */
class IPAddress6 : public ::in6_addr {
public:
    IPAddress6();
    IPAddress6(::in6_addr addr6);
    IPAddress6(const IPAddress6 &other) = default;

    /**
     * @brief Convert ipv6 address to human readable string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    
    /**
     * @brief Get the readonly span of the data
     * 
     * @tparam T 
     * @return std::span<const T> 
     */
    template <typename T = uint8_t>
    auto span() const -> std::span<const T, sizeof(::in6_addr) / sizeof(T)>;

    /**
     * @brief Check this ipv6 address is any
     * 
     * @return true 
     * @return false 
     */
    auto isAny() const -> bool;
    /**
     * @brief Check this ipv6 address is none
     * 
     * @return true 
     * @return false 
     */
    auto isNone() const -> bool;
    /**
     * @brief Check this ipv6 address is loopback
     * 
     * @return true 
     * @return false 
     */
    auto isLoopback() const -> bool;
    /**
     * @brief Check this ipv6 address is multicast
     * 
     * @return true 
     * @return false 
     */
    auto isMulticast() const -> bool;

    auto operator ==(const IPAddress6 &other) const -> bool;
    auto operator !=(const IPAddress6 &other) const -> bool;

    /**
     * @brief Get the any ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto any() -> IPAddress6;
    /**
     * @brief Get the none ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto none() -> IPAddress6;
    /**
     * @brief Get the loop ipv6 address
     * 
     * @return IPAddress6 
     */
    static auto loopback() -> IPAddress6;
    /**
     * @brief Parse the ipv6 address from string
     * 
     * @param value 
     * @return IPAddress6 
     */
    static auto fromString(const char *value) -> IPAddress6;
    /**
     * @brief Parse the ipv6 address from hostname
     * 
     * @param hostname 
     * @return IPAddress6 
     */
    static auto fromHostname(const char *hostname) -> IPAddress6;
};

/**
 * @brief Abstraction of v4, v6 
 * 
 */
class IPAddress {
public:
    IPAddress();
    IPAddress(::in_addr addr4);
    IPAddress(::in6_addr addr6);
    IPAddress(const char *str);
    IPAddress(const IPAddress &) = default;

    /**
     * @brief Convert current address to human readable string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    /**
     * @brief Get the family of this address, like AF_INET or AF_INET6
     * 
     * @return int 
     */
    auto family() const -> int;
    /**
     * @brief Get the length of this address, like 4 or 16
     * 
     * @return int 
     */
    auto length() const -> int;
    /**
     * @brief Check this address is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;

    /**
     * @brief Get the raw pointer of stored data
     * 
     * @return const void* 
     */
    auto data() const -> const void *;
    /**
     * @brief Get the raw pointer of stored data
     * 
     * @return void* 
     */
    auto data() -> void *;
    /**
     * @brief Cast to the referenced type
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto data() const -> const T &;
    /**
     * @brief Cast to the referenced type
     * 
     * @tparam T 
     * @return T& 
     */
    template <typename T>
    auto data() -> T &;

    /**
     * @brief Get the span of the contained data
     * 
     * @tparam T 
     * @return std::span<const T> 
     */
    template <typename T = uint8_t>
    auto span() const -> std::span<const T>;

    /**
     * @brief Compare the endpoint, check it is same
     * 
     * @param rhs 
     * @return true 
     * @return false 
     */
    auto compare(const IPAddress &rhs) const -> bool;
    auto operator ==(const IPAddress &rhs) const -> bool;
    auto operator !=(const IPAddress &rhs) const -> bool;

    /**
     * @brief Parse the ip string
     * 
     * @param str 
     * @return IPAddress 
     */
    static auto fromString(const char *str) -> IPAddress;
    /**
     * @brief Parse the hostname 
     * 
     * @param hostname 
     * @return IPAddress 
     */
    static auto fromHostname(const char *hostname) -> IPAddress;
    /**
     * @brief Copy network-format ip address from buffer
     * 
     * @param data The pointer to the buffer
     * @param size The size of the buffer (must be 4 or 16)
     * @return IPAddress 
     */
    static auto fromRaw(const void *data, size_t size) -> IPAddress;
    template <typename T>
    static auto fromRaw(const T &data) -> IPAddress;
private:
    union {
        ::in_addr v4;
        ::in6_addr v6;
    } mStorage;
    enum {
        None = AF_UNSPEC,
        V4 = AF_INET,
        V6 = AF_INET6,
        Unix = AF_UNIX,
    } mFamily = None;
};

// --- IPAddress4 Impl
inline IPAddress4::IPAddress4() { }
inline IPAddress4::IPAddress4(::in_addr addr) : ::in_addr(addr) { }

inline auto IPAddress4::toString() const -> std::string {
    return ::inet_ntoa(*this);
}
inline auto IPAddress4::toUint32() const -> uint32_t {
    return ::ntohl(toUint32NetworkOrder());
}
inline auto IPAddress4::toUint32NetworkOrder() const -> uint32_t {
    return reinterpret_cast<const uint32_t&>(*this);
}
template <typename T>
inline auto IPAddress4::span() const -> std::span<const T, sizeof(::in_addr) / sizeof(T)> {
    static_assert(sizeof(::in_addr) % sizeof(T) == 0, "sizeof mismatch");
    constexpr auto n = sizeof(::in_addr) / sizeof(T);
    auto addr = reinterpret_cast<const T*>(this);
    return std::span<const T, n>(addr, n);
}

inline auto IPAddress4::isAny() const -> bool {
    return toUint32() == INADDR_ANY;
}
inline auto IPAddress4::isNone() const -> bool {
    return toUint32() == INADDR_NONE;
}
inline auto IPAddress4::isLoopback() const -> bool {
    return toUint32() == INADDR_LOOPBACK;
}
inline auto IPAddress4::isBroadcast() const -> bool {
    return toUint32() == INADDR_BROADCAST;
}
inline auto IPAddress4::isMulticast() const -> bool {
    return IN_MULTICAST(toUint32());
}
inline auto IPAddress4::operator ==(const IPAddress4 &other) const -> bool {
    return toUint32NetworkOrder() == other.toUint32NetworkOrder();
}
inline auto IPAddress4::operator !=(const IPAddress4 &other) const -> bool {
    return toUint32NetworkOrder() != other.toUint32NetworkOrder();
}

inline auto IPAddress4::any() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_ANY);
}
inline auto IPAddress4::loopback() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_LOOPBACK);   
}
inline auto IPAddress4::broadcast() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_BROADCAST);
}
inline auto IPAddress4::none() -> IPAddress4 {
    return IPAddress4::fromUint32(INADDR_NONE);
}
inline auto IPAddress4::fromRaw(const void *raw, size_t size) -> IPAddress4 {
    ILIAS_ASSERT(size == sizeof(::in_addr));
    return *static_cast<const ::in_addr*>(raw);
}
inline auto IPAddress4::fromString(const char *address) -> IPAddress4 {
    IPAddress4 addr;
    if (::inet_pton(AF_INET, address, &addr) != 1) {
        return IPAddress4::none();
    }
    return addr;
}
inline auto IPAddress4::fromHostname(const char *hostnamne) -> IPAddress4 {
    auto ent = ::gethostbyname(hostnamne);
    if (!ent || ent->h_addrtype != AF_INET) {
        return IPAddress4::none();
    }
    return *reinterpret_cast<const IPAddress4*>(ent->h_addr_list[0]);
}
inline auto IPAddress4::fromUint32(uint32_t uint32) -> IPAddress4 {
    static_assert(sizeof(uint32_t) == sizeof(::in_addr), "sizeof mismatch");
    uint32 = ::htonl(uint32);
    return reinterpret_cast<::in_addr&>(uint32);
}
inline auto IPAddress4::fromUint32NetworkOrder(uint32_t uint32) -> IPAddress4 {
    return reinterpret_cast<::in_addr&>(uint32);
}

// --- IPAddress6 Impl
inline IPAddress6::IPAddress6() { }
inline IPAddress6::IPAddress6(::in6_addr addr) : ::in6_addr(addr) { }

inline auto IPAddress6::toString() const -> std::string {
    char buf[INET6_ADDRSTRLEN] {0};
    ::inet_ntop(AF_INET6, this, buf, sizeof(buf));
    return buf;
}
template <typename T>
inline auto IPAddress6::span() const -> std::span<const T, sizeof(::in6_addr) / sizeof(T)> {
    static_assert(sizeof(::in6_addr) % sizeof(T) == 0, "sizeof mismatch");
    constexpr auto n = sizeof(::in6_addr) / sizeof(T);
    auto addr = reinterpret_cast<const T*>(this);
    return std::span<const T, n>(addr, n);
}
inline auto IPAddress6::isAny() const -> bool {
    return IN6_IS_ADDR_UNSPECIFIED(this);
}
inline auto IPAddress6::isNone() const -> bool {
    return IN6_IS_ADDR_UNSPECIFIED(this);
}
inline auto IPAddress6::isLoopback() const -> bool {
    return IN6_IS_ADDR_LOOPBACK(this);
}
inline auto IPAddress6::isMulticast() const -> bool {
    return IN6_IS_ADDR_MULTICAST(this);
}
inline auto IPAddress6::operator ==(const IPAddress6 &addr) const -> bool {
    return IN6_ARE_ADDR_EQUAL(this, &addr);
}
inline auto IPAddress6::operator !=(const IPAddress6 &addr) const -> bool {
    return !IN6_ARE_ADDR_EQUAL(this, &addr);
}

inline auto IPAddress6::any() -> IPAddress6 {
    return ::in6_addr IN6ADDR_ANY_INIT;
}
inline auto IPAddress6::none() -> IPAddress6 {
    return ::in6_addr IN6ADDR_ANY_INIT;
}
inline auto IPAddress6::loopback() -> IPAddress6 {
    return ::in6_addr IN6ADDR_LOOPBACK_INIT;
}
inline auto IPAddress6::fromString(const char *str) -> IPAddress6 {
    IPAddress6 addr;
    if (::inet_pton(AF_INET6, str, &addr) != 1) {
        return IPAddress6::any();
    }
    return addr;
}

// --- IPAddress Impl
inline IPAddress::IPAddress() { }
inline IPAddress::IPAddress(::in_addr addr) : mFamily(V4) { mStorage.v4 = addr; }
inline IPAddress::IPAddress(::in6_addr addr) : mFamily(V6) { mStorage.v6 = addr; }
inline IPAddress::IPAddress(const char *addr) {
    // IPV6 contains ':'
    mFamily = ::strchr(addr, ':') ? V6 : V4;
    if (::inet_pton(mFamily, addr, &mStorage) != 1) {
        mFamily = None;
    }
}

inline auto IPAddress::isValid() const -> bool {
    return mFamily != None;
}
inline auto IPAddress::family() const -> int {
    return mFamily;
}
inline auto IPAddress::length() const -> int {
    switch (mFamily) {
        case V4:  return sizeof(::in_addr);
        case V6:  return sizeof(::in6_addr);
        default:  return 0;
    }
}
inline auto IPAddress::toString() const -> std::string {
    if (!isValid()) {
        return std::string();
    }
    char buf[INET6_ADDRSTRLEN] {0};
    ::inet_ntop(family(), &mStorage, buf, sizeof(buf));
    return buf;
}

template <typename T>
inline auto IPAddress::data() const -> const T & {
    return *reinterpret_cast<const T *>(&mStorage);
}
inline auto IPAddress::data() const -> const void * {
    return &mStorage;
}
inline auto IPAddress::data() -> void * {
    return &mStorage;
}

template <typename T>
inline auto IPAddress::span() const -> std::span<const T> {
    switch (mFamily) {
        case V4: return data<IPAddress4>().span<T>();
        case V6: return data<IPAddress6>().span<T>();
        default: return {};
    }
}

inline auto IPAddress::compare(const IPAddress &other) const -> bool {
    if (family() != other.family()) {
        return false;
    }
    switch (mFamily) {
        case V4:  return data<IPAddress4>() == other.data<IPAddress4>();
        case V6:  return data<IPAddress6>() == other.data<IPAddress6>();
        default:  return true; //< All are invalid address
    }
}
inline auto IPAddress::operator ==(const IPAddress &other) const -> bool {
    return compare(other);
}
inline auto IPAddress::operator !=(const IPAddress &other) const -> bool {
    return !compare(other);
}

inline auto IPAddress::fromString(const char *str) -> IPAddress {
    return IPAddress(str);
}
inline auto IPAddress::fromHostname(const char *hostname) -> IPAddress {
    auto ent = ::gethostbyname(hostname);
    if (!ent) {
        return IPAddress();
    }
    return IPAddress::fromRaw(ent->h_addr_list[0], ent->h_length);
}
inline auto IPAddress::fromRaw(const void *raw, size_t len) -> IPAddress {
    switch (len) {
        case sizeof(::in_addr): {
            return IPAddress(*static_cast<const ::in_addr *>(raw));
        }
        case sizeof(::in6_addr): {
            return IPAddress(*static_cast<const ::in6_addr *>(raw));
        }
        default: {
            return IPAddress();
        }
    }
}
template <typename T>
inline auto IPAddress::fromRaw(const T &data) -> IPAddress {
    static_assert(sizeof(T) == sizeof(::in_addr) || sizeof(T) == sizeof(::in6_addr), "Invalid size");
    return IPAddress::fromRaw(&data, sizeof(T));
}

ILIAS_NS_END