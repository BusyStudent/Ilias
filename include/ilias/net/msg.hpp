/**
 * @file msg.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapper for sendmsg and recvmsg's structures
 * @version 0.1
 * @date 2024-12-03
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once
#include <ilias/net/endpoint.hpp>
#include <ilias/net/system.hpp>
#include <span>

#if defined(_WIN32) //< redirect the names to the windows ones
    #define msg_name name
    #define msg_namelen namelen
    #define msg_iov lpBuffers
    #define msg_iovlen dwBufferCount
    #define msg_control  Control.buf
    #define msg_controllen Control.len
    #define msg_flags dwFlags
    #define iov_base buf
    #define iov_len  len
#endif


ILIAS_NS_BEGIN

/**
 * @brief Wrapper for system iovec on linux, WSABUF on windows
 * 
 */
class IoVec : public iovec_t {
public:
    IoVec() = default;
    IoVec(const IoVec &) = default;
    IoVec(const iovec_t &v) : iovec_t(v) { }

    /**
     * @brief Construct a new Io Vec object
     * 
     * @param buf The buffer to be used
     * @param size The size of the buffer
     */
    IoVec(void *buf, size_t size) {
        iov_base = static_cast<char*>(buf); //< In Windows this field is a char*
        iov_len = size;
    }

    /**
     * @brief Construct a new Io Vec object from std::span<std::byte>
     * 
     * @param buffer 
     */
    IoVec(std::span<std::byte> buffer) {
        iov_base = reinterpret_cast<char*>(buffer.data());
        iov_len = buffer.size();
    }

    /**
     * @brief Explicit Construct a new Io Vec object from std::span<const std::byte>
     * @note This is need explicitly because of the const_cast
     * @param buffer 
     */
    explicit IoVec(std::span<const std::byte> buffer) {
        iov_base = const_cast<char*>(reinterpret_cast<const char*>(buffer.data()));
        iov_len = buffer.size();
    }

    /**
     * @brief Get the data pointer
     * 
     * @return void* 
     */
    auto data() const -> void* { return iov_base; }

    /**
     * @brief Get the size of the buffer
     * 
     * @return size_t 
     */
    auto size() const -> size_t { return iov_len; }

    /**
     * @brief Allow conversion to std::span<std::byte>
     * 
     * @return std::span<std::byte> 
     */
    operator std::span<std::byte>() const noexcept {
        return {
            reinterpret_cast<std::byte*>(iov_base),
            iov_len
        };
    }

    /**
     * @brief Allow conversion to std::span<const std::byte>
     * 
     * @return std::span<const std::byte> 
     */
    operator std::span<const std::byte>() const noexcept {
        return {
            reinterpret_cast<const std::byte*>(iov_base),
            iov_len
        };
    }
};

static_assert(sizeof(IoVec) == sizeof(iovec_t));


/**
 * @brief Wrapper for system msghdr on linux, WSAMSG on windows
 * 
 */
class MsgHdr : public msghdr_t {
public:
    MsgHdr(const msghdr_t &v) : msghdr_t(v) { }
    MsgHdr(const MsgHdr &) = default;
    MsgHdr() {
        ::memset(this, 0, sizeof(msghdr_t));
    }
    
    /**
     * @brief Set the endpoint buffer
     * 
     * @param addr 
     * @param len 
     */
    auto setEndpoint(::sockaddr *addr, ::socklen_t len) -> void {
        msg_name = addr;
        msg_namelen = len;
    }

    /**
     * @brief Set the Endpoint object
     * 
     * @param addr 
     * @param len 
     */
    auto setEndpoint(const ::sockaddr *addr, ::socklen_t len) -> void {
        msg_name = const_cast<::sockaddr*>(addr);
        msg_namelen = len;
    }

    /**
     * @brief Set the Buffers object, the number of buffers to use
     * 
     * @param buffers 
     */
    auto setBuffers(std::span<IoVec> buffers) -> void {
        msg_iov = buffers.data();
        msg_iovlen = buffers.size();
    }

    /**
     * @brief Set the Control buffer object
     * 
     * @param control 
     */
    auto setControl(std::span<std::byte> control) -> void {
        msg_control = reinterpret_cast<char*>(control.data());
        msg_controllen = control.size();
    }

    /**
     * @brief Set the Control buffer object
     * 
     * @param control 
     * @param len 
     */
    auto setControl(void *control, size_t len) -> void {
        msg_control = reinterpret_cast<char*>(control);
        msg_controllen = len;
    }

    /**
     * @brief Get the byte span of the endpoint in the message
     * 
     * @return std::span<std::byte> 
     */
    auto endpoint() const -> std::span<std::byte> {
        return {
            reinterpret_cast<std::byte*>(msg_name),
            size_t(msg_namelen)
        };
    }

    /**
     * @brief Get the flags of the message on receive
     * 
     * @return int 
     */
    auto flags() const -> int {
        return msg_flags;
    }

    /**
     * @brief Get the span of the buffers in the message
     * 
     * @return std::span<IoVec> 
     */
    auto buffers() -> std::span<IoVec> {
        return {
            static_cast<IoVec*>(msg_iov),
            msg_iovlen
        };
    }
};

static_assert(sizeof(MsgHdr) == sizeof(msghdr_t));

ILIAS_NS_END


#if defined(_WIN32)
    #undef msg_name
    #undef msg_namelen
    #undef msg_iov
    #undef msg_iovlen
    #undef msg_control
    #undef msg_controllen
    #undef iov_base
    #undef iov_len
#endif