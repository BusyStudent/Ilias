#pragma once
#include <ilias/net/endpoint.hpp>
#include <ilias/io/error.hpp>
#include "overlapped.hpp"
#include <atomic> // std::atomic
#include <latch> // std::latch

ILIAS_NS_BEGIN

namespace win32 {

// MARK: Net
/**
 * @brief Awaiter wrapping WSASendTo
 * 
 */
class IocpSendtoAwaiter final : public IocpAwaiter<IocpSendtoAwaiter> {
public:
    IocpSendtoAwaiter(SOCKET sock, Buffer buffer, int flags, EndpointView endpoint) :
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

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        ILIAS_TRACE("IOCP", "WSASendTo {} bytes on sockfd {} completed, Error {}", bytesTransferred, sockfd(), error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
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

/**
 * @brief Awaiter wrapping WSARecvFrom
 * 
 */
class IocpRecvfromAwaiter final : public IocpAwaiter<IocpRecvfromAwaiter> {
public:
    IocpRecvfromAwaiter(SOCKET sock, MutableBuffer buffer, int flags, MutableEndpointView endpoint) :
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

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        ILIAS_TRACE("IOCP", "WSARecvFrom {} bytes on sockfd {} completed, Error {}", bytesTransferred, sockfd(), error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
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

/**
 * @brief Awaiter wrapping ConnectEx
 * 
 */
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

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<void> {
        ILIAS_TRACE("IOCP", "Connect To {} on sockfd {} completed, Error {}", mEndpoint, sockfd(), error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
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

/**
 * @brief Awaiter wrapping AcceptEx
 * 
 */
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

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<socket_t> {
        ILIAS_TRACE("IOCP", "Accept on sockfd {} completed, acceptedSock {} Error {}", sockfd(), mAcceptedSock, error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
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
                return Err(IoError::InvalidArgument);
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

/**
 * @brief Awaiter wrapper for TransmitFile
 * 
 */
class IocpSendfileAwaiter final : public IocpAwaiter<IocpSendfileAwaiter> {
public:
    IocpSendfileAwaiter(SOCKET sock, HANDLE file, size_t offset, DWORD size, LPFN_TRANSMITFILE transmitFile) : 
        IocpAwaiter(sock), mFile(file), mSize(size), mTransmitFile(transmitFile)
    {
        setOffset(offset);
    }

    auto onSubmit() -> bool {
        return mTransmitFile(
            sockfd(),
            mFile,
            mSize,
            0,
            overlapped(),
            nullptr,
            0
        );
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    HANDLE mFile;
    DWORD  mSize;

    LPFN_TRANSMITFILE mTransmitFile;
};

/**
 * @brief Awaiter wrapper for WSASendMsg
 * 
 */
class IocpSendmsgAwaiter final : public IocpAwaiter<IocpSendmsgAwaiter> {
public:
    IocpSendmsgAwaiter(SOCKET sock, const WSAMSG &msg, DWORD flags, LPFN_WSASENDMSG sendMsg) :
        IocpAwaiter(sock), mMsg(msg), mFlags(flags), mSendMsg(sendMsg)
    {

    }

    auto onSubmit() -> bool {
        return mSendMsg(
            sockfd(),
            &mMsg,
            mFlags,
            &bytesTransferred(),
            overlapped(),
            nullptr
        ) == 0;
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    WSAMSG mMsg;
    DWORD  mFlags;

    LPFN_WSASENDMSG mSendMsg = nullptr;
};

/**
 * @brief Awaiter wrapper for WSARecvMsg
 * 
 */
class IocpRecvmsgAwaiter final : public IocpAwaiter<IocpRecvmsgAwaiter> {
public:
    IocpRecvmsgAwaiter(SOCKET sock, WSAMSG &msg, DWORD flags, LPFN_WSARECVMSG recvMsg) :
        IocpAwaiter(sock), mMsg(msg), mFlags(flags), mRecvMsg(recvMsg)
    {
        mMsg.dwFlags = mFlags; // Acroding to the MSDN docs, the flags are put in the WSAMSG struct
    }

    auto onSubmit() -> bool {
        return mRecvMsg(
            sockfd(),
            &mMsg,
            &bytesTransferred(),
            overlapped(),
            nullptr
        ) == 0;
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    WSAMSG &mMsg;
    DWORD   mFlags;

    LPFN_WSARECVMSG mRecvMsg = nullptr;
};

// MARK: Fs
/**
 * @brief Wrapping the iocp async read operations
 * 
 */
class IocpReadAwaiter final : public IocpAwaiter<IocpReadAwaiter> {
public:
    IocpReadAwaiter(HANDLE handle, MutableBuffer buffer, std::optional<size_t> offset) :
        IocpAwaiter(handle), mBuffer(buffer) 
    {
        if (offset) {
            overlapped()->setOffset(offset.value());
        }
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "ReadFile {} bytes on handle {}", mBuffer.size(), handle());
        return ::ReadFile(handle(), mBuffer.data(), mBuffer.size(), &bytesTransferred(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        ILIAS_TRACE("IOCP", "ReadFile {} bytes on handle {} completed, Error {}", bytesTransferred, handle(), error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    MutableBuffer mBuffer;
};

/**
 * @brief Wrapping the iocp async write operations
 * 
 */
class IocpWriteAwaiter final : public IocpAwaiter<IocpWriteAwaiter> {
public:
    IocpWriteAwaiter(HANDLE handle, Buffer buffer, std::optional<size_t> offset) :
        IocpAwaiter(handle), mBuffer(buffer)
    {
        if (offset) {
            overlapped()->setOffset(offset.value());
        }
    }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "WriteFile {} bytes on handle {}", mBuffer.size(), handle());
        return ::WriteFile(handle(), mBuffer.data(), mBuffer.size(), &bytesTransferred(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        ILIAS_TRACE("IOCP", "WriteFile {} bytes on handle {} completed, Error {}", bytesTransferred, handle(), error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    Buffer mBuffer;
};

/**
 * @brief Wrapping the ConnectNamedPipe async operations
 * 
 */
class IocpConnectPipeAwaiter final : public IocpAwaiter<IocpConnectPipeAwaiter> {
public:
    IocpConnectPipeAwaiter(HANDLE handle) : IocpAwaiter(handle) { }

    auto onSubmit() -> bool {
        ILIAS_TRACE("IOCP", "ConnectNamedPipe on handle {}", handle());
        return ::ConnectNamedPipe(handle(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<void> {
        ILIAS_TRACE("IOCP", "ConnectNamedPipe on handle {} completed, Error {}", handle(), error);
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        return {};
    }
};

class IocpDeviceIoControlAwaiter final : public IocpAwaiter<IocpDeviceIoControlAwaiter> {
public:
    IocpDeviceIoControlAwaiter(HANDLE handle, DWORD controlCode, MutableBuffer inBuffer, MutableBuffer outBuffer) :
        IocpAwaiter(handle), mControlCode(controlCode), mInBuffer(inBuffer), mOutBuffer(outBuffer) 
    {
        
    }

    auto onSubmit() -> bool {
        return ::DeviceIoControl(handle(), mControlCode, mInBuffer.data(), mInBuffer.size(), mOutBuffer.data(), mOutBuffer.size(), &bytesTransferred(), overlapped());
    }

    auto onComplete(DWORD error, DWORD bytesTransferred) -> IoResult<size_t> {
        if (error != ERROR_SUCCESS) {
            return Err(SystemError(error));
        }
        return bytesTransferred;
    }
private:
    DWORD mControlCode;
    MutableBuffer mInBuffer;
    MutableBuffer mOutBuffer;
};

// Get the function pointer of the winsock extension
inline auto WSAGetExtensionFnPtr(SOCKET sockfd, GUID id, void *fnptr) -> Result<void, SystemError> {
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
        return Err(SystemError::fromErrno());
    }
    return {};
}

// Do io call in thread pool
// MARK: SynchronousIo
template <typename Fn>
inline auto ioCall(const runtime::StopToken &token, Fn fn) -> std::invoke_result_t<Fn> {
    // Get the thread handle, used for CancelSynchronousIo
    HANDLE threadHandle = nullptr;
    auto ok = ::DuplicateHandle(
        ::GetCurrentProcess(),
        ::GetCurrentThread(),
        ::GetCurrentProcess(),
        &threadHandle,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS
    );
    if (!ok) {
        return Err(SystemError::fromErrno());
    }

    struct Guard {
        ~Guard() {
            ::CloseHandle(h);
        }
        HANDLE h;
    } guard{threadHandle};

    // Check the stop request
    std::invoke_result_t<Fn> result = Err(SystemError::Canceled); // store the return value
    std::atomic<HANDLE> handle {threadHandle}; // nullptr on (canceled or completed)
    std::latch latch {1};
    runtime::StopCallback callback(token, [&]() {
        if (auto h = handle.exchange(nullptr); h != nullptr) {
            ::CancelSynchronousIo(h);
            latch.count_down();
        }
    });
    if (!token.stop_requested()) {
        result = fn();
    }
    if (handle.exchange(nullptr) == nullptr) { // Check the cancel is start?, if start, wait for it, if not, mark as completed
        latch.wait();
    }
    return result;
}

}

ILIAS_NS_END