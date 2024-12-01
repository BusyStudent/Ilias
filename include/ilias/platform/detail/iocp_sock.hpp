/**
 * @file iocp_sock.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Impl the basic socket operations using IOCP
 * @version 0.1
 * @date 2024-08-17
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/platform/detail/iocp_overlapped.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/net/endpoint.hpp>
#include <ilias/log.hpp>
#include <WinSock2.h>
#include <MSWSock.h>
#include <span>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Awaiter wrapping WSASendTo
 * 
 */
class IocpSendtoAwaiter final : public IocpAwaiter<IocpSendtoAwaiter> {
public:
    IocpSendtoAwaiter(SOCKET sock, std::span<const std::byte> buffer, int flags, EndpointView endpoint) :
        IocpAwaiter(sock)
    {
        mBuf.buf = (char*) buffer.data();
        mBuf.len = buffer.size();
        mFlags = flags;
        if (endpoint) {
            mAddr = endpoint.data();
            mAddrLen = endpoint.length();
        }
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "WSASendTo {} bytes on sockfd {}", mBuf.len, sockfd());
        if (!mAddr) { //< No endpoint provided, we don't need to fill it, use WSASend
            return ::WSASend(sockfd(), &mBuf, 1, &bytesTransferred(), mFlags, overlapped(), nullptr) == 0;
        }
        return ::WSASendTo(
            sockfd(),
            &mBuf,
            1,
            &bytesTransferred(),
            mFlags,
            mAddr,
            mAddrLen,
            overlapped(),
            nullptr
        ) == 0;
    }

    auto onComplete(::DWORD error, DWORD bytesTransferred) -> Result<size_t> {
        ILIAS_TRACE("IOCP", "WSASendTo {} bytes on sockfd {} completed, Error {}", bytesTransferred, sockfd(), error);
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    // Args
    ::WSABUF          mBuf;
    ::DWORD           mFlags = 0;
    const ::sockaddr *mAddr = nullptr;
    int               mAddrLen = 0;
};

class IocpRecvfromAwaiter final : public IocpAwaiter<IocpRecvfromAwaiter> {
public:
    IocpRecvfromAwaiter(SOCKET sock, std::span<std::byte> buffer, int flags, MutableEndpointView endpoint) :
        IocpAwaiter(sock) 
    {
        mBuf.buf = (char*) buffer.data();
        mBuf.len = buffer.size();
        mFlags = flags;
        mAddr = endpoint.data();
        mAddrLen = endpoint.bufsize();
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "WSARecvFrom {} bytes on sockfd {}", mBuf.len, sockfd());
        if (!mAddr) { //< No endpoint provided, we don't need to fill it, use WSARecv
            // Acrording to microsof's documentation, the sock returned by AcceptEx only can use WSARecv, not WSARecvFrom
            return ::WSARecv(sockfd(), &mBuf, 1, &bytesTransferred(), &mFlags, overlapped(), nullptr) == 0;
        }
        return ::WSARecvFrom(
            sockfd(),
            &mBuf,
            1,
            &bytesTransferred(),
            &mFlags,
            mAddr,
            &mAddrLen,
            overlapped(),
            nullptr
        ) == 0;
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> Result<size_t> {
        ILIAS_TRACE("IOCP", "WSARecvFrom {} bytes on sockfd {} completed, Error {}", bytesTransferred, sockfd(), error);
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    // Args
    ::WSABUF    mBuf;
    ::DWORD     mFlags = 0;
    ::sockaddr *mAddr = nullptr;
    int         mAddrLen = 0;
};

class IocpConnectAwaiter final : public IocpAwaiter<IocpConnectAwaiter> {
public:
    IocpConnectAwaiter(SOCKET sock, EndpointView endpoint, LPFN_CONNECTEX connectEx) :
        IocpAwaiter(sock), mEndpoint(endpoint), mConnectEx(connectEx)
    {

    }

    auto onSubmit() -> bool {
        // The ConnectEx says the sockfd must be already bound to an address
        // so we check, if it's not the case we bind it to a random address
        ::WSAPROTOCOL_INFO info;
        ::socklen_t infoLen = sizeof(info);
        ::sockaddr_storage addr;
        ::socklen_t len = sizeof(addr);
        if (::getsockname(sockfd(), reinterpret_cast<::sockaddr*>(&addr), &len) != 0) {
            if (::getsockopt(sockfd(), SOL_SOCKET, SO_PROTOCOL_INFO, (char*) &info, &infoLen) != 0) {
                return false;
            }
            // Not binded
            ::memset(&addr, 0, sizeof(addr));
            addr.ss_family = info.iAddressFamily;
            if (::bind(sockfd(), reinterpret_cast<::sockaddr*>(&addr), mEndpoint.length()) != 0) {
                return false;
            }
        }

        ILIAS_TRACE("IOCP", "Connect To on sockfd {}", sockfd());

        return mConnectEx(
            sockfd(), 
            mEndpoint.data(), 
            mEndpoint.length(),
            nullptr,
            0,
            nullptr,
            overlapped()
        );
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> Result<void> {
        ILIAS_TRACE("IOCP", "Connect To on sockfd {} completed, Error {}", sockfd(), error);
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        // Update Connection context
        if (::setsockopt(sockfd(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) != 0) {
            ILIAS_WARN("IOCP", "Failed to update connect context on sockfd {}", sockfd());
        }
        return {};
    }
private:
    EndpointView mEndpoint;
    LPFN_CONNECTEX mConnectEx = nullptr;
};

class IocpAcceptAwaiter final : public IocpAwaiter<IocpAcceptAwaiter> {
public:
    IocpAcceptAwaiter(SOCKET sock, MutableEndpointView endpoint, LPFN_ACCEPTEX acceptEx, LPFN_GETACCEPTEXSOCKADDRS getAcceptExSockaddrs) :
        IocpAwaiter(sock), mEndpoint(endpoint), mAcceptEx(acceptEx), mGetAcceptExSockaddrs(getAcceptExSockaddrs)
    {

    }
    ~IocpAcceptAwaiter() {
        if (mAcceptedSock != INVALID_SOCKET) {
            ::closesocket(mAcceptedSock);
        }
    }

    auto onSubmit() -> bool {
        // Frist create a new socket for the accepted connection
        ::sockaddr_storage addr;
        ::socklen_t len = sizeof(addr);
        if (::getsockname(sockfd(), reinterpret_cast<::sockaddr*>(&addr), &len) != 0) {
            return false;
        }
        mAcceptedSock = ::socket(addr.ss_family, SOCK_STREAM, 0);
        if (mAcceptedSock == INVALID_SOCKET) {
            return false;
        }
        
        ILIAS_TRACE("IOCP", "Accept on sockfd {}", sockfd());

        return mAcceptEx(
            sockfd(),
            mAcceptedSock,
            mAddressBuf,
            0,
            sizeof(::sockaddr_storage) + 16,
            sizeof(::sockaddr_storage) + 16,
            &bytesTransferred(),
            overlapped()
        );
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> Result<socket_t> {
        ILIAS_TRACE("IOCP", "Accept on sockfd {} completed, acceptedSock {} Error {}", sockfd(), mAcceptedSock, error);
        if (error != ERROR_SUCCESS) {
            return Unexpected(SystemError(error));
        }
        // Get the accepted connection address
        ::sockaddr_storage *localAddr = nullptr, *remoteAddr = nullptr;
        ::socklen_t localAddrLen = 0, remoteAddrLen = 0;

        mGetAcceptExSockaddrs(
            mAddressBuf, bytesTransferred,
            sizeof(::sockaddr_storage) + 16,
            sizeof(::sockaddr_storage) + 16,
            reinterpret_cast<::sockaddr**>(&localAddr), &localAddrLen,
            reinterpret_cast<::sockaddr**>(&remoteAddr), &remoteAddrLen
        );

        // Update the accepted connection context
        auto listener = sockfd();
        if (::setsockopt(mAcceptedSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*) &listener, sizeof(listener)) != 0) {
            ILIAS_WARN("IOCP", "Failed to update accept context for sockfd {}, Error {}", mAcceptedSock, ::GetLastError());
        }

        if (mEndpoint) {
            if (remoteAddrLen > mEndpoint.bufsize()) { //< Endpoint buffer is too small
                return Unexpected(Error::InvalidArgument);
            }
            ::memcpy(mEndpoint.data(), remoteAddr, remoteAddrLen);
        }
        auto sock = mAcceptedSock;
        mAcceptedSock = INVALID_SOCKET;
        return sock;
    }
private:
    MutableEndpointView mEndpoint = nullptr;
    SOCKET mAcceptedSock = INVALID_SOCKET;
    std::byte mAddressBuf[(sizeof(::sockaddr_storage) + 16) * 2];

    LPFN_ACCEPTEX mAcceptEx = nullptr;
    LPFN_GETACCEPTEXSOCKADDRS mGetAcceptExSockaddrs = nullptr;
};

inline auto WSAGetExtensionFnPtr(::SOCKET sockfd, GUID id, void *fnptr) -> Result<void> {
    DWORD bytes = 0;
    auto ret = ::WSAIoctl(
        sockfd,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &id,
        sizeof(id),
        fnptr,
        sizeof(fnptr),
        &bytes,
        nullptr,
        nullptr
    );
    if (ret != 0) {
        return Unexpected(SystemError::fromErrno());
    }
    return {};
}

} // namespace detail

ILIAS_NS_END