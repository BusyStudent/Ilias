/**
 * @file msghdr.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapper for sendmsg and recvmsg's structures
 * @version 0.1
 * @date 2024-12-03
 * 
 * @copyright Copyright (c) 2024
 * 
 */
// Experimental!!!
#pragma once
#include <ilias/net/endpoint.hpp> // IPEndoint
#include <ilias/net/system.hpp> // ILIAS_MSGHDR_T
#include <ilias/io/vec.hpp> // IoVec
#include <span>

#if defined(_WIN32) // Redirect the names to the windows ones
    #define msg_name name
    #define msg_namelen namelen
    #define msg_iov lpBuffers
    #define msg_iovlen dwBufferCount
    #define msg_control  Control.buf
    #define msg_controllen Control.len
    #define msg_flags dwFlags
#endif // _WIN32

ILIAS_NS_BEGIN

using msghdr_t = ILIAS_MSGHDR_T;

/**
 * @brief The const version for system msghdr, used for sendmsg
 * 
 */
class MsgHdr : public msghdr_t {
public:
    constexpr MsgHdr(const msghdr_t &v) : msghdr_t(v) { }
    constexpr MsgHdr(const MsgHdr &) = default;
    constexpr MsgHdr() : msghdr_t{} { }

    /**
     * @brief Set the Endpoint object
     * 
     * @param addr The const endpoint pointer
     * @param len The endpoint length
     */
    auto setEndpoint(const ::sockaddr *addr, ::socklen_t len) noexcept -> void {
        msg_name = const_cast<::sockaddr*>(addr);
        msg_namelen = len;
    }

    auto setEndpoint(EndpointView endpoint) noexcept -> void {
        msg_name = const_cast<::sockaddr*>(endpoint.data());
        msg_namelen = endpoint.length();
    }

    /**
     * @brief Set the Buffers object, the number of buffers to use
     * 
     * @param buffers 
     */
    auto setBuffers(std::span<const IoVec> buffers) noexcept -> void {
        // It should fine ? the recvmsg and sendmsg should not modify the buffer's pointers values
        msg_iov = toSystem(const_cast<IoVec*>(buffers.data()));
        msg_iovlen = buffers.size();
    }

    /**
     * @brief Get the flags of the message on receive
     * 
     * @return int 
     */
    auto flags() const noexcept -> int {
        return msg_flags;
    }
};

/**
 * @brief The mutable version for system msghdr, used for sendmsg & recvmsg
 * 
 */
class MutableMsgHdr final : public MsgHdr {
public:
    constexpr MutableMsgHdr(const msghdr_t &v) : MsgHdr(v) { }
    constexpr MutableMsgHdr(const MutableMsgHdr &) = default;
    constexpr MutableMsgHdr(const MsgHdr &) = delete; // Doesn't allow from const to mutable
    constexpr MutableMsgHdr() : MsgHdr{} { }

    auto setEndpoint(::sockaddr *addr, ::socklen_t len) noexcept -> void {
        msg_name = addr;
        msg_namelen = len;
    }

    auto setEndpoint(MutableEndpointView endpoint) noexcept -> void {
        msg_name = endpoint.data();
        msg_namelen = endpoint.bufsize();
    }

    auto setBuffers(std::span<const MutableIoVec> buffers) noexcept -> void {
        // It should fine ? the recvmsg and sendmsg should not modify the buffer's pointers values
        msg_iov = toSystem(const_cast<MutableIoVec*>(buffers.data()));
        msg_iovlen = buffers.size();
    }

    // Inherit flags from MsgHdr
    using MsgHdr::flags;
};

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
#endif // _WIN32