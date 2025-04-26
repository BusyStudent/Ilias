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

#include <ilias/net/address.hpp>
#include <ilias/net/system.hpp>
#include <ilias/error.hpp>
#include <charconv>
#include <cstring>
#include <compare>

ILIAS_NS_BEGIN

/**
 * @brief The concept of endpoint
 * 
 * @tparam T 
 */
template <typename T>
concept Endpoint = requires(T t) {
    t.data();
    t.length();
};

template <typename T>
concept MutableEndpoint = requires(T t) {
    t.data();
    t.bufsize();
};


#if !defined(ILIAS_NO_AF_UNIX)

/**
 * @brief The endpoint of a unix domain socket
 * 
 */
class UnixEndpoint : public ::sockaddr_un {
public:
    UnixEndpoint() = default;

    /**
     * @brief Construct a new Unix Endpoint object by raw ::sockaddr_un
     * 
     * @param addr The network-format unix endpoint address
     */
    UnixEndpoint(::sockaddr_un addr) : ::sockaddr_un(addr) { }

    /**
     * @brief Construct a new Unix Endpoint object based on path
     * 
     * @param path The path of the unix endpoint (must be shorter than sizeof(sun_path) - 1)
     */
    UnixEndpoint(std::string_view path) {
        auto maxlen = sizeof(sun_path) - 1;
        auto len = path.size() >= maxlen ? maxlen : path.size();
        ::memset(this, 0, sizeof(::sockaddr_un));
        ::memcpy(sun_path, path.data(), len);
        sun_path[len] = '\0';
        sun_family = AF_UNIX;
    }

    template <size_t N>
    UnixEndpoint(const char (&path)[N]) {
        static_assert(N < sizeof(sun_path), "The path is too long!");
        ::memset(this, 0, sizeof(::sockaddr_un));
        ::memcpy(sun_path, path, N - 1);
        sun_path[N] = '\0';
        sun_family = AF_UNIX;
    }

    /**
     * @brief Get the data of the endpoint
     * 
     * @return void* 
     */
    auto data() -> void * {
        return this;
    }

    /**
     * @brief Get the data of the endpoint
     * 
     * @return const void* 
     */
    auto data() const -> const void * {
        return this;
    }

    /**
     * @brief Get the family of the endpoint
     * 
     * @return int 
     */
    auto family() const -> int {
        return AF_UNIX;
    }

    /**
     * @brief Get the length of the endpoint
     * 
     * @return size_t 
     */
    auto length() const -> size_t {
        return sizeof(::sockaddr_un);
    }

    /**
     * @brief Get the maximum buffer size of the endpoint
     * 
     * @return size_t 
     */
    auto bufsize() const -> size_t {
        return sizeof(::sockaddr_un);
    }

    /**
     * @brief Get the path of the endpoint
     * 
     * @return std::string_view 
     */
    auto path() const -> std::string_view {
        if (!isValid()) {
            return {};
        }
        if (isAbstract()) {
            return {sun_path, sizeof(sun_path)};
        }
        return {sun_path, ::strnlen(sun_path, sizeof(sun_path))};
    }

    /**
     * @brief Convert the endpoint to string
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        if (!isValid()) {
            return {};
        }
        return sun_path;
    }

    /**
     * @brief Check if the endpoint is valid
     * 
     * @return true 
     * @return false 
     */
    auto isValid() const -> bool {
        return sun_family == AF_UNIX;
    }

    /**
     * @brief Check if the endpoint is abstract path
     * 
     * @return true 
     * @return false 
     */
    auto isAbstract() const -> bool {
        return sun_path[0] == '\0';
    }

    /**
     * @brief Parse a string to unix endpoint
     * 
     * @param path 
     * @return Result<UnixEndpoint> 
     */
    static auto fromString(std::string_view path) -> Result<UnixEndpoint> {
        if (path.size() >= sizeof(sun_path)) {
            return Unexpected(Error::InvalidArgument);
        }
        return UnixEndpoint(path);
    }
};

#endif // defined(ILIAS_NO_AF_UNIX)

/**
 * @brief The endpoint of a internet socket
 * 
 */
class IPEndpoint {
public:
    IPEndpoint() = default;

    /**
     * @brief Construct a new IPEndpoint object
     * 
     * @param addr The network-format ipv4 endpoint
     */
    IPEndpoint(::sockaddr_in addr) { ::memcpy(&mAddr4, &addr, sizeof(addr)); }

    /**
     * @brief Construct a new IPEndpoint object
     * 
     * @param addr The network-format ipv6 endpoint
     */
    IPEndpoint(::sockaddr_in6 addr) { ::memcpy(&mAddr6, &addr, sizeof(addr)); }

    /**
     * @brief Construct a new IPEndpoint object by string
     * 
     * @param str The endpoint in string format (address4:port) or ([address6]:port)
     */
    IPEndpoint(std::string_view str) : IPEndpoint(fromString(str).value_or(IPEndpoint { })) { }

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
        switch (addr.family()) {
            case AF_INET: {
                auto &sockaddr = cast<::sockaddr_in>();
                ::memset(&sockaddr, 0, sizeof(sockaddr));
                sockaddr.sin_family = AF_INET;
                sockaddr.sin_port = hostToNetwork(port); 
                sockaddr.sin_addr = addr.cast<::in_addr>();
                break;
            }
            case AF_INET6: {
                auto &sockaddr = cast<::sockaddr_in6>();
                ::memset(&sockaddr, 0, sizeof(sockaddr));
                sockaddr.sin6_family = AF_INET6;
                sockaddr.sin6_port = hostToNetwork(port); 
                sockaddr.sin6_addr = addr.cast<::in6_addr>();
                break;
            }
            default: mData.sa_family = addr.family(); break;
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
            case AF_INET: return networkToHost(cast<::sockaddr_in>().sin_port);
            case AF_INET6: return networkToHost(cast<::sockaddr_in6>().sin6_port);
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
        return mData.sa_family;
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
     * @brief Get the maximum buffer size of the endpoint (the max sizeof address you can store in it)
     * 
     * @return size_t 
     */
    auto bufsize() const -> size_t {
        static_assert(sizeof(::sockaddr_in6) >= sizeof(::sockaddr_in));
        return sizeof(::sockaddr_in6);
    }

    /**
     * @brief Compare two ip endpoint
     * 
     * @param other 
     */
    auto operator ==(const IPEndpoint &other) const {
        if (family() != other.family()) {
            return false;
        }
        if (isValid()) {
            return ::memcmp(data(), other.data(), length()) == 0;
        }
        return true; // Both are invalid
    }

    /**
     * @brief Compare two ip endpoint
     * 
     * @param other 
     * @return auto 
     */
    auto operator !=(const IPEndpoint &other) const {
        return !(*this == other);
    }

    /**
     * @brief Compare two ip endpoint
     * 
     * @param other
     * @return std::strong_ordering
     */
    auto operator <=>(const IPEndpoint &other) const {
        if (family() != other.family()) {
            return family() <=> other.family();
        }
        if (isValid()) {
            return ::memcmp(data(), other.data(), length()) <=> 0;
        }
        return std::strong_ordering::equal; // Both are invalid
    }

    /**
     * @brief Parse endpoint from string
     * 
     * @param buffer The endpoint in string format (address4:port) or ([address6]:port)
     * @return Result<IPEndpoint> 
     */
    static auto fromString(std::string_view buffer) -> Result<IPEndpoint> {
        // Split to addr and port
        auto pos = buffer.find_last_of(':');
        if (pos == buffer.npos || pos < 4) { // 4 is the minimum length of [::]:port
            return Unexpected(Error::InvalidArgument);
        }

        // Parse the port
        auto portStr = buffer.substr(pos + 1);
        uint16_t port = 0;
        if (std::from_chars(portStr.data(), portStr.data() + portStr.size(), port).ec != std::errc()) {
            return Unexpected(Error::InvalidArgument);
        }

        // Parse the address
        auto addrStr = buffer.substr(0, pos);
        if (addrStr.front() == '[' && addrStr[pos - 1] == ']') {
            // IPV6
            addrStr = addrStr.substr(1, pos - 2);
        }
        auto addr = IPAddress::fromString(addrStr);
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

    /**
     * @brief Check if the endpoint is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return isValid();
    }
private:
    union {
        ::sockaddr     mData { .sa_family = AF_UNSPEC };
        ::sockaddr_in  mAddr4;
        ::sockaddr_in6 mAddr6;
    };
};

/**
 * @brief A const view of an endpoint
 * 
 */
class EndpointView {
public:
    /**
     * @brief Construct a new empty Endpoint View object
     * 
     */
    EndpointView() = default;

    /**
     * @brief Construct a new empty Endpoint View object
     * 
     */
    EndpointView(std::nullptr_t) : mAddr(nullptr), mLength(0) { }

    /**
     * @brief Construct a new Endpoint View object
     * 
     * @param addr The pointer to the endpoint
     * @param len The length to the endpoint
     */
    EndpointView(const ::sockaddr *addr, ::socklen_t len) : mAddr(addr), mLength(len) { }

    /**
     * @brief Construct a new Endpoint View object from any endpoint like object
     * 
     * @tparam T must has Endpoint concept
     * @param ep The const reference to the endpoint
     */
    template <Endpoint T>
    EndpointView(const T &ep) : mAddr(reinterpret_cast<const ::sockaddr*>(ep.data())), mLength(ep.length()) { }

    /**
     * @brief Construct a new Endpoint View object from any endpoint like object's ptr
     * 
     * @tparam T must has Endpoint concept
     * @param ep The const pointer to the endpoint (can be nullptr)
     */
    template <Endpoint T>
    EndpointView(T *ep) {
        if (ep) {
            mAddr = reinterpret_cast<const ::sockaddr*>(ep->data());
            mLength = ep->length();
        }
    }

    /**
     * @brief Construct a new Endpoint View object by copying from another view
     * 
     */
    EndpointView(const EndpointView &) = default;

    /**
     * @brief Get the data pointer to the endpoint
     * 
     * @return const ::sockaddr* 
     */
    auto data() const -> const ::sockaddr * {
        return mAddr;
    }

    /**
     * @brief Get the length of the endpoint
     * 
     * @return ::socklen_t 
     */
    auto length() const -> ::socklen_t {
        return mLength;
    }

    /**
     * @brief Convert endpoint view to human readable string (for debug use)
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        if (!mAddr) {
            return "EndpointView(null)";
        }
        if (auto family = mAddr->sa_family; family == AF_INET || family == AF_INET6) {
            return "EndpointView(" + IPEndpoint::fromRaw(mAddr, mLength).value().toString() + ")";
        }
        char buffer[256] {0};
        ::snprintf(buffer, sizeof(buffer), "EndpointView(.family = %d, .len = %d)", int(mAddr->sa_family), int(mLength));
        return buffer;
    }

    /**
     * @brief Compare with another EndpointView
     * 
     */
    auto operator <=>(const EndpointView &) const noexcept = default;

    /**
     * @brief Check if the view is not empty
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mAddr != nullptr;
    }
private:
    const ::sockaddr *mAddr = nullptr;
    ::socklen_t       mLength = 0;
};

/**
 * @brief A view for receiving endpoint's data
 * 
 */
class MutableEndpointView {
public:
    /**
     * @brief Construct a new empty Mutable Endpoint View object
     * 
     */
    MutableEndpointView() = default;

    /**
     * @brief Construct a new empty Mutable Endpoint View object
     * 
     */
    MutableEndpointView(std::nullptr_t) : mAddr(nullptr), mBufSize(0) { }

    /**
     * @brief Construct a new Mutable Endpoint View object
     * 
     * @param addr The pointer to the mutable endpoint
     * @param bufsize The max buffer size of the endpoint to store
     */
    MutableEndpointView(::sockaddr *addr, ::socklen_t bufsize) : mAddr(addr), mBufSize(bufsize) { }

    /**
     * @brief Construct a new Endpoint View object from any endpoint like object
     * 
     * @tparam T must has Endpoint concept
     * @param ep The mutable reference to the endpoint
     */
    template <MutableEndpoint T>
    MutableEndpointView(T &ep) : mAddr(reinterpret_cast<::sockaddr*>(ep.data())), mBufSize(ep.bufsize()) { }

    /**
     * @brief Construct a new Endpoint View object from any endpoint like object's ptr
     * 
     * @tparam T must has Endpoint concept
     * @param ep The mutable pointer to the endpoint (can be nullptr)
     */
    template <MutableEndpoint T>
    MutableEndpointView(T *ep) {
        if (ep) {
            mAddr = reinterpret_cast<::sockaddr*>(ep->data());
            mBufSize = ep->bufsize();
        }
    }

    /**
     * @brief Construct a new Mutable Endpoint View object by copying from another view
     * 
     */
    MutableEndpointView(const MutableEndpointView &) = default;

    /**
     * @brief Get the data pointer to the endpoint
     * 
     */
    auto data() const -> ::sockaddr * {
        return mAddr;
    }

    /**
     * @brief Get the bufsize of the endpoint
     * 
     * @return ::socklen_t 
     */
    auto bufsize() const -> ::socklen_t {
        return mBufSize;
    }

    /**
     * @brief Convert the endpoint to human readable string (debug use)
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        if (!mAddr) {
            return "MutableEndpointView(null)";
        }
        char buffer[256] {0};
        ::snprintf(buffer, sizeof(buffer), "MutableEndpointView(.ptr = %p, .bufsize = %d)", mAddr, int(mBufSize));
        return buffer;
    }

    /**
     * @brief Compare with another MutableEndpointView
     * 
     */
    auto operator <=>(const MutableEndpointView &) const noexcept = default;

    /**
     * @brief Check if the view is not empty
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mAddr != nullptr;
    }
private:
    ::sockaddr *mAddr = nullptr;
    ::socklen_t mBufSize = 0;
};

ILIAS_NS_END