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

#pragma once

#include <ilias/detail/expected.hpp>
#include <ilias/net/address.hpp>
#include <ilias/net/system.hpp>
#include <charconv>

ILIAS_NS_BEGIN

class IPEndpoint {
public:
    IPEndpoint() = default;

    /**
     * @brief Construct a new IPEndpoint object
     * 
     * @param addr The network-format ipv4 endpoint
     */
    IPEndpoint(::sockaddr_in addr) { ::memcpy(&mData, &addr, sizeof(addr)); }

    /**
     * @brief Construct a new IPEndpoint object
     * 
     * @param addr The network-format ipv6 endpoint
     */
    IPEndpoint(::sockaddr_in6 addr) { ::memcpy(&mData, &addr, sizeof(addr)); }

    /**
     * @brief Construct a new IPEndpoint object
     * 
     * @param addr The network-format endpoint
     */
    IPEndpoint(::sockaddr_storage addr) : mData(addr) { }

    /**
     * @brief Construct a new IPEndpoint object by string
     * 
     * @param str The endpoint in string format (address4:port) or ([address6]:port)
     */
    IPEndpoint(std::string_view str) : IPEndpoint(fromString(str).value_or(IPEndpoint {})) { }

    /**
     * @brief Construct a new IPEndpoint object by string
     * 
     * @param str The endpoint in string format (address4:port) or ([address6]:port)
     */
    IPEndpoint(const std::string &str) : IPEndpoint(std::string_view(str)) { }

    /**
     * @brief Construct a new IPEndpoint object by string
     * 
     * @param str The endpoint in string format (address4:port) or ([address6]:port)
     */
    IPEndpoint(const char *str) : IPEndpoint(std::string_view(str)) { }

    /**
     * @brief Construct a new IPEndpoint object by address and port
     * 
     * @param addr The wrapped ipv4/ipv6 address
     * @param port The port number in host byte order
     */
    IPEndpoint(const IPAddress &addr, uint16_t port) {
        mData.ss_family = addr.family();
        switch (addr.family()) {
            case AF_INET: {
                auto &sockaddr = cast<::sockaddr_in>();
                sockaddr.sin_port = ::htons(port); 
                sockaddr.sin_addr = addr.cast<::in_addr>();
                break;
            }
            case AF_INET6: {
                auto &sockaddr = cast<::sockaddr_in6>();
                sockaddr.sin6_port = ::htons(port); 
                sockaddr.sin6_addr = addr.cast<::in6_addr>();
                break;
            }
        }
    }

    /**
     * @brief Construct a new IPEndpoint object by copy
     * 
     */
    IPEndpoint(const IPEndpoint &) = default;

    /**
     * @brief Convert endpoint to string
     * 
     * @return std::string (ip:port) on ipv4, ([ip]:port) on ipv6
     */
    auto toString() const -> std::string {
        std::string ret;
        if (!isValid()) {
            return ret;
        }
        if (family() == AF_INET) {
            ret += address().toString();
        }
        else {
            ret += '[';
            ret += address().toString();
            ret += ']';
        }
        ret += ':';
        ret += std::to_string(port());
        return ret;
    }

    /**
     * @brief Get The address part of endpoint
     * 
     * @return IPAddress 
     */
    auto address() const -> IPAddress {
        switch (family()) {
            case AF_INET: return IPAddress(cast<::sockaddr_in>().sin_addr);
            case AF_INET6: return IPAddress(cast<::sockaddr_in6>().sin6_addr);
            default: return IPAddress();
        }
    }

    /**
     * @brief Get the address part of endpoint and cast to IPv4
     * 
     * @return IPAddress4 
     */
    auto address4() const -> IPAddress4 {
        ILIAS_ASSERT(family() == AF_INET);
        return cast<::sockaddr_in>().sin_addr;
    }

    /**
     * @brief Get the address part of endpoint and cast to IPv6
     * 
     * @return IPAddress6 
     */
    auto address6() const -> IPAddress6 {
        ILIAS_ASSERT(family() == AF_INET6);
        return cast<::sockaddr_in6>().sin6_addr;
    }

    /**
     * @brief Get the port part of the endpoint
     * 
     * @return uint16_t 
     */
    auto port() const -> uint16_t {
        switch (family()) {
            case AF_INET: return ::ntohs(cast<::sockaddr_in>().sin_port);
            case AF_INET6: return ::ntohs(cast<::sockaddr_in6>().sin6_port);
            default: return 0;
        }
    }

    /**
     * @brief Get the endpoint length in bytes
     * 
     * @return socklen_t 
     */
    auto length() const -> socklen_t {
        switch (family()) {
            case AF_INET: return sizeof(::sockaddr_in);
            case AF_INET6: return sizeof(::sockaddr_in6);
            default: return 0;
        }
    }

    /**
     * @brief Get the family of the endpoint
     * 
     * @return int 
     */
    auto family() const -> int {
        return mData.ss_family;
    }

    /**
     * @brief Get the raw data pointer of the endpoint
     * 
     * @return const void* 
     */
    auto data() const -> const void * {
        return &mData;
    }

    /**
     * @brief Get the raw data pointer of the endpoint
     * 
     * @return void* 
     */
    auto data() -> void * {
        return &mData;
    }

    /**
     * @brief Cast to network-format data to another typr
     * 
     * @tparam T 
     * @return const T& 
     */
    template <typename T>
    auto cast() const -> const T & {
        return reinterpret_cast<const T &>(mData);
    }

    /**
     * @brief Cast to network-format data to another typr
     * 
     * @tparam T 
     * @return T& 
     */
    template <typename T>
    auto cast() -> T & {
        return reinterpret_cast<T &>(mData);
    }

    /**
     * @brief Check if the endpoint is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool {
        return family() != AF_UNSPEC;
    }

    /**
     * @brief Parse endpoint from string
     * 
     * @param str The endpoint in string format (address4:port) or ([address6]:port)
     * @return Result<IPEndpoint> 
     */
    static auto fromString(std::string_view str) -> Result<IPEndpoint> {
        std::string buffer(str);

        // Split to addr and port
        auto pos = buffer.find_last_of(':');
        if (pos == buffer.npos || pos == 0) {
            return Unexpected(Error::InvalidArgument);
        }
        buffer[pos] = '\0';

        // Parse the port
        uint16_t port = 0;
        if (std::from_chars(buffer.c_str() + pos + 1, buffer.c_str() + buffer.size(), port).ec != std::errc()) {
            return Unexpected(Error::InvalidArgument);
        }

        // Parse the address
        const char *begin = buffer.c_str();
        if (buffer.front() == '[' && buffer[pos - 1] == ']') {
            // IPV6
            buffer[pos - 1] = '\0';
            begin = buffer.c_str() + 1;
        }
        auto addr = IPAddress::fromString(begin);
        if (!addr) {
            return Unexpected(addr.error());
        }
        return IPEndpoint(*addr, port);
    }

    /**
     * @brief Copy endpoint from network-format data
     * 
     * @param data The pointer to the data
     * @param size The size of the data, must be sizeof(::sockaddr_in) or sizeof(::sockaddr_in6)
     * @return IPEndpoint 
     */
    static auto fromRaw(const void *mem, size_t n) -> Result<IPEndpoint> {
        switch (n) {
            case sizeof(::sockaddr_in): return *reinterpret_cast<const ::sockaddr_in *>(mem);
            case sizeof(::sockaddr_in6): return *reinterpret_cast<const ::sockaddr_in6 *>(mem);
            default: return Unexpected(Error::InvalidArgument);
        }
    }
private:
    ::sockaddr_storage mData {
        .ss_family = AF_UNSPEC
    };
};

ILIAS_NS_END

// --- Formatter for IPEndpoint
#if defined(__cpp_lib_format)
template <>
struct std::formatter<ILIAS_NAMESPACE::IPEndpoint> {
    constexpr auto parse(std::format_parse_context &ctxt) {
        return ctxt.begin();
    }
    auto format(const ILIAS_NAMESPACE::IPEndpoint &addr, std::format_context &ctxt) const {
        return std::format_to(ctxt.out(), "{}", addr.toString());
    }
};
#endif