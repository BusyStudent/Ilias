#pragma once

/**
 * @file endpoint.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For wrapping sockaddr_storage
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "address.hpp"
#include "sys.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Abstraction of sockaddr_storage (address + port)
 * 
 */
class IPEndpoint {
public:
    IPEndpoint();
    IPEndpoint(const char *str);
    IPEndpoint(const std::string &str);
    IPEndpoint(std::string_view str);
    IPEndpoint(::sockaddr_in addr4);
    IPEndpoint(::sockaddr_in6 addr6);
    IPEndpoint(::sockaddr_storage storage);
    IPEndpoint(const IPAddress &address, uint16_t port);
    IPEndpoint(const IPEndpoint &) = default;

    /**
     * @brief Cast to human readable string (ip:port)
     * 
     * @return std::string 
     */
    auto toString() const -> std::string;
    /**
     * @brief Get the ipv4 address of the endpoint
     * 
     * @return IPAddress4 
     */
    auto address4() const -> IPAddress4;
    /**
     * @brief Get the ipv6 address of the endpoint
     * 
     * @return IPAddress6 
     */
    auto address6() const -> IPAddress6;
    /**
     * @brief Get the address of the endpoint
     * 
     * @return IPAddress 
     */
    auto address() const -> IPAddress;
    /**
     * @brief Get the port of the endpoint
     * 
     * @return uint16_t 
     */
    auto port() const -> uint16_t;
    /**
     * @brief Get the family of the endpoint
     * 
     * @return int 
     */
    auto family() const -> int;
    /**
     * @brief Get the length of the endpoint
     * 
     * @return int 
     */
    auto length() const -> int;
    /**
     * @brief Check the endpoint is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool;
    /**
     * @brief Get the data of the current endpoint
     * 
     * @return const void* 
     */
    auto data() const -> const void *;
    /**
     * @brief Cast to endpoint data to
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto data() const -> const T &;

    /**
     * @brief Get the data of the current endpoint
     * 
     * @return void* 
     */
    auto data() -> void *;
    /**
     * @brief Cast to endpoint data to
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto data() -> T &;

    /**
     * @brief Compare the endpoint is same
     * 
     * @param rhs 
     * @return true 
     * @return false 
     */
    auto compare(const IPEndpoint &rhs) const -> bool;
    auto operator ==(const IPEndpoint &rhs) const -> bool;
    auto operator !=(const IPEndpoint &rhs) const -> bool;

    /**
     * @brief Parse string to endpoint
     * 
     * @param str 
     * @return IPEndpoint 
     */
    static auto fromString(std::string_view str) -> IPEndpoint;
    /**
     * @brief Copy endpoint from network-format data
     * 
     * @param data The pointer to the data
     * @param size The size of the data, must be sizeof(::sockaddr_in) or sizeof(::sockaddr_in6)
     * @return IPEndpoint 
     */
    static auto fromRaw(const void *data, size_t size) -> IPEndpoint;
    template <typename T>
    static auto fromRaw(const T &data) -> IPEndpoint;
private:
    ::sockaddr_storage mAddr { };
};

// --- IPEndpoint Impl
inline IPEndpoint::IPEndpoint() { }
inline IPEndpoint::IPEndpoint(const char *str) : IPEndpoint(fromString(str)) { }
inline IPEndpoint::IPEndpoint(std::string_view str) : IPEndpoint(fromString(str)) { }
inline IPEndpoint::IPEndpoint(const std::string &str) : IPEndpoint(fromString(str)) { }
inline IPEndpoint::IPEndpoint(::sockaddr_in addr4) { ::memcpy(&mAddr, &addr4, sizeof(addr4)); }
inline IPEndpoint::IPEndpoint(::sockaddr_in6 addr6) { ::memcpy(&mAddr, &addr6, sizeof(addr6)); }
inline IPEndpoint::IPEndpoint(::sockaddr_storage addr) : mAddr(addr) { }
inline IPEndpoint::IPEndpoint(const IPAddress &addr, uint16_t port) {
    mAddr.ss_family = addr.family();
    switch (mAddr.ss_family) {
        case AF_INET: {
            auto &sockaddr = data<::sockaddr_in>();
            sockaddr.sin_port = ::htons(port);
            sockaddr.sin_addr = addr.data<::in_addr>();
            break;
        }
        case AF_INET6: {
            auto &sockaddr = data<::sockaddr_in6>();
            sockaddr.sin6_port = ::htons(port);
            sockaddr.sin6_addr = addr.data<::in6_addr>();
            break;
        }
    }
}

inline auto IPEndpoint::isValid() const -> bool {
    return mAddr.ss_family != 0;
}
inline auto IPEndpoint::family() const -> int {
    return mAddr.ss_family;
}
inline auto IPEndpoint::length() const -> int {
    switch (mAddr.ss_family) {
        case AF_INET: return sizeof(::sockaddr_in);
        case AF_INET6: return sizeof(::sockaddr_in6);
        default: return 0;
    }
}
inline auto IPEndpoint::port() const -> uint16_t {
    switch (mAddr.ss_family) {
        case AF_INET: 
            return ::ntohs(data<::sockaddr_in>().sin_port);
        case AF_INET6: 
            return ::ntohs(data<::sockaddr_in6>().sin6_port);
        default : return 0;
    }
}
inline auto IPEndpoint::address() const -> IPAddress {
    switch (mAddr.ss_family) {
        case AF_INET: 
            return IPAddress::fromRaw(data<::sockaddr_in>().sin_addr);
        case AF_INET6: 
            return IPAddress::fromRaw(data<::sockaddr_in6>().sin6_addr);
        default: return IPAddress();
    }
}
inline auto IPEndpoint::address4() const -> IPAddress4 {
    ILIAS_ASSERT(mAddr.ss_family == AF_INET);
    return IPAddress4(data<::sockaddr_in>().sin_addr);
}
inline auto IPEndpoint::address6() const -> IPAddress6 {
    ILIAS_ASSERT(mAddr.ss_family == AF_INET6);
    return IPAddress6(data<::sockaddr_in6>().sin6_addr);
}
inline auto IPEndpoint::toString() const -> std::string {
    if (!isValid()) {
        return std::string();
    }
    if (family() == AF_INET6) {
        return '[' + address().toString() + ']' + ':' + std::to_string(port());
    }
    return address().toString() + ':' + std::to_string(port());
}
inline auto IPEndpoint::data() const -> const void * {
    return &mAddr;
}
template <typename T>
inline auto IPEndpoint::data() const -> const T & {
    return *reinterpret_cast<const T*>(&mAddr);
}
template <typename T>
inline auto IPEndpoint::data() -> T & {
    return *reinterpret_cast<T*>(&mAddr);
}

inline auto IPEndpoint::compare(const IPEndpoint &other) const -> bool {
    return family() == other.family() && address() == other.address() && port() == other.port();
}
inline auto IPEndpoint::operator ==(const IPEndpoint &other) const -> bool {
    return compare(other);
}
inline auto IPEndpoint::operator !=(const IPEndpoint &other) const -> bool {
    return !compare(other);
}

inline auto IPEndpoint::fromString(std::string_view str) -> IPEndpoint {
    std::string buffer(str);

    // Split to addr and port
    auto pos = buffer.find(':');
    if (pos == buffer.npos || pos == 0) {
        return IPEndpoint();
    }
    buffer[pos] = '\0';
    
    int port = 0;
    if (::sscanf(buffer.c_str() + pos + 1, "%d", &port) != 1) {
        return IPEndpoint();
    }
    IPAddress addr;
    if (buffer.front() == '[' && buffer[pos - 1] == ']') {
        buffer[pos - 1] = '\0';
        addr = IPAddress::fromString(buffer.c_str() + 1);
    }
    else {
        addr = IPAddress::fromString(buffer.c_str());
    }
    return IPEndpoint(addr, port);
}
inline auto IPEndpoint::fromRaw(const void *raw, size_t len) -> IPEndpoint {
    switch (len) {
        case sizeof(::sockaddr_in):
            return IPEndpoint(*static_cast<const ::sockaddr_in*>(raw));
        case sizeof(::sockaddr_in6):
            return IPEndpoint(*static_cast<const ::sockaddr_in6*>(raw));
        case sizeof(::sockaddr_storage):
            return IPEndpoint(*static_cast<const ::sockaddr_storage*>(raw));
        default:
            return IPEndpoint();
    }
}
template <typename T>
inline auto IPEndpoint::fromRaw(const T &raw) -> IPEndpoint {
    static_assert(sizeof(T) == sizeof(::sockaddr_in) || 
                  sizeof(T) == sizeof(::sockaddr_in6) ||
                  sizeof(T) == sizeof(::sockaddr_storage), 
                  "Invalid raw type"
    );
    return fromRaw(&raw, sizeof(T));
}

ILIAS_NS_END