/**
 * @file context.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The IoContext class, provides the context for io operations, such as file io, socket io, timer, etc.
 * @version 0.1
 * @date 2024-08-11
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/runtime/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/io/error.hpp>
#include <ilias/buffer.hpp>
#include <optional>
#include <cstddef>
#include <memory>
#include <span>

ILIAS_NS_BEGIN

class MsgHdr;
class IoContext;
class EndpointView;
class MutableMsgHdr;
class MutableEndpointView;

/**
 * @brief The IoDescriptor class, user should only take the pointer to this class, it is opaque to the user
 * 
 */
class IoDescriptor {
public:
    struct Deleter;

    // RAII pointer for the descriptor
    using Ptr = std::unique_ptr<IoDescriptor, Deleter>;

    /**
     * @brief The type of the descriptor, used on addDescriptor
     * 
     */
    enum Type {
        Socket,   //< Socket descriptor
        File,     //< Generic file descriptor
        Tty,      //< TTY descriptor
        Pipe,     //< Pipe descriptor
        Unknown,  //< Unknown type, let the backend decide by os api
        Pollable, //< Unknown type, but a pollable object, such as a timer, eventfd, etc.
        User,     //< User defined type, used for custom more type for backend
    };
protected:
    IoDescriptor() = default;
    ~IoDescriptor() = default;
};

/**
 * @brief The IoContext class, provides the context for io operations, such as file io, socket io, timer, etc.
 * 
 */
class ILIAS_API IoContext : public runtime::Executor {
public:
    /**
     * @brief Add a new system descriptor to the context
     * 
     * @param fd 
     * @param type 
     * @return IoResult<IoDescriptor*> 
     */
    virtual auto addDescriptor(fd_t fd, IoDescriptor::Type type) -> IoResult<IoDescriptor*> = 0;

    /**
     * @brief Remove a descriptor from the context, it will cancel all async operations on this descriptor
     * 
     * @param fd
     * @return IoResult<void> 
     */
    virtual auto removeDescriptor(IoDescriptor *fd) -> IoResult<void> = 0;

    /**
     * @brief Cancel all pending Io operations on a descriptor
     * 
     * @param fd
     * @return IoResult<void> 
     */
    virtual auto cancel(IoDescriptor *fd) -> IoResult<void> = 0;

    /**
     * @brief Read from a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return IoTask<size_t> 
     */
    virtual auto read(IoDescriptor *fd, MutableBuffer buffer, std::optional<size_t> offset) -> IoTask<size_t> = 0;

    /**
     * @brief Write to a descriptor
     * 
     * @param fd 
     * @param buffer 
     * @param offset The offset in the file, std::nullopt means on ignore
     * @return IoTask<size_t> 
     */
    virtual auto write(IoDescriptor *fd, Buffer buffer, std::optional<size_t> offset) -> IoTask<size_t> = 0;

    /**
     * @brief Connect to a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param endpoint 
     * @return IoTask<void> 
     */
    virtual auto connect(IoDescriptor *fd, EndpointView endpoint) -> IoTask<void> = 0;

    /**
     * @brief Accept a connection
     * 
     * @param fd The fd must be a listening socket
     * @param remoteEndpoint The endpoint of the remote, if nullptr, the endpoint will be ignored
     * @return IoTask<socket_t> 
     */
    virtual auto accept(IoDescriptor *fd, MutableEndpointView remoteEndpoint) -> IoTask<socket_t> = 0;

    /**
     * @brief Send data to a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to send
     * @param flags The flags to use, like MSG_DONTWAIT
     * @param endpoint The endpoint to send to, if nullptr, the fd must be connected
     * @return IoTask<size_t> 
     */
    virtual auto sendto(IoDescriptor *fd, Buffer buffer, int flags, EndpointView endpoint) -> IoTask<size_t> = 0;

    /**
     * @brief Receive data from a remote endpoint
     * 
     * @param fd The fd must be a socket
     * @param buffer The buffer to receive into
     * @param flags The flags to use, like MSG_DONTWAIT
     * @param endpoint The endpoint to receive from
     * @return IoTask<size_t> 
     */
    virtual auto recvfrom(IoDescriptor *fd, MutableBuffer buffer, int flags, MutableEndpointView endpoint) -> IoTask<size_t> = 0;

    /**
     * @brief Poll a descriptor for events
     * 
     * @param fd The fd must be pollable (socket on most systems)
     * @param events The events to poll, like POLLIN, POLLOUT, etc
     * @return IoTask<uint32_t> 
     */
    virtual auto poll(IoDescriptor *fd, uint32_t events) -> IoTask<uint32_t> = 0;

    // Advanced Io Operations
    /**
     * @brief Send message to a socket
     * 
     * @param fd The fd must be a socket
     * @param msg The message to send
     * @param flags The flags to use, like MSG_DONTWAIT
     * @return IoTask<size_t> 
     */
    virtual auto sendmsg(IoDescriptor *fd, const MsgHdr &msg, int flags) -> IoTask<size_t> = 0;

    /**
     * @brief Receive message from a socket
     * 
     * @param fd The fd must be a socket
     * @param msg The message to receive into
     * @param flags The flags to use, like MSG_DONTWAIT
     * @return IoTask<size_t> 
     */
    virtual auto recvmsg(IoDescriptor *fd, MutableMsgHdr &msg, int flags) -> IoTask<size_t> = 0;
    
    /**
     * @brief Get the current thread io context
     * 
     * @return IoContext* 
     */
    static auto currentThread() -> IoContext * {
        return static_cast<IoContext*>(Executor::currentThread());
    }

#if defined(_WIN32)
    // Win32 Specific Io Operations
    /**
     * @brief Wrapping Win32 ConnectNamedPipe, The named pipe server wait for a client to connect
     * 
     * @param fd The fd must be a named pipe
     * @return IoTask<void> 
     */
    virtual auto connectNamedPipe(IoDescriptor *fd) -> IoTask<void>;

    /**
     * @brief Wrapping Win32 WaitForSingleObject, wait for a object to be signaled
     * 
     * @param object 
     * @return IoTask<void> 
     */
    virtual auto waitObject(void *object) -> IoTask<void>;
#endif // defined(_WIN32)

};

/**
 * @brief Deleter of the IoDescriptor, it contains a ctxt, used to remove it
 * 
 */
struct IoDescriptor::Deleter {
    IoContext *ctxt = nullptr;

    auto operator()(IoDescriptor *desc) const -> void {
        auto _ = ctxt->removeDescriptor(desc);
    }
};

/**
 * @brief A RAII Wrapper for fd_t + IoDescriptor
 * 
 * @param T The fd type, like HANDLE, int, SOCKET, Socket
 */
template <IntoFileDescriptor T>
class IoHandle {
public:
    explicit IoHandle(IoDescriptor::Ptr desc, T fd) : mDesc(std::move(desc)), mFd(std::move(fd)) {}
    IoHandle(IoHandle &&other) = default;
    IoHandle() = default;
    ~IoHandle() = default;

    /**
     * @brief Close the IoHandle's descriptor and fd
     * 
     */
    auto close() -> void {
        mDesc.reset();
        mFd = {};
    }

    /**
     * @brief Close the IoHandle's descriptor and return the fd
     * @warning The fd may not useable on iocp backend
     * @return T 
     */
    auto detach() -> T {
        mDesc.reset();
        return std::move(mFd);
    }

    /**
     * @brief Get the wrapped fd
     * 
     * @return const T& 
     */
    auto fd() const noexcept -> const T & {
        return mFd;
    }

    /**
     * @brief Get the Io Context
     * 
     * @return IoContext* 
     */
    auto context() const noexcept -> IoContext * {
        return mDesc.get_deleter().ctxt;
    }

    // Forward to IoContext
    auto cancel() const {
        return context()->cancel(mDesc.get());
    }

    auto write(auto &&...args) const {
        return context()->write(mDesc.get(), args...);
    }

    auto read(auto &&...args) const {
        return context()->read(mDesc.get(), args...);
    }

    auto poll(auto &&...args) const {
        return context()->poll(mDesc.get(), args...);
    }

    auto connect(auto &&...args) const {
        return context()->connect(mDesc.get(), args...);
    }

    auto accept(auto &&...args) const {
        return context()->accept(mDesc.get(), args...);
    }

    auto sendto(auto &&...args) const {
        return context()->sendto(mDesc.get(), args...);
    }

    auto recvfrom(auto &&...args) const {
        return context()->recvfrom(mDesc.get(), args...);
    }

    auto sendmsg(auto &&...args) const {
        return context()->sendmsg(mDesc.get(), args...);
    }

    auto recvmsg(auto &&...args) const {
        return context()->recvmsg(mDesc.get(), args...);
    }

#if defined(_WIN32)
    auto connectNamedPipe() const {
        return context()->connectNamedPipe(mDesc.get());
    }
#endif

    // Operators
    auto operator <=>(const IoHandle &other) const noexcept = default;
    auto operator =(IoHandle &&other) noexcept -> IoHandle & = default;

    explicit operator bool() const noexcept {
        return bool(mDesc);
    }
    
    /**
     * @brief Create an wrapped IoHandle from an fd
     * 
     * @param ctxt The IoContext to use
     * @param fd   The fd to wrap
     * @param type The type of the fd (default: Unknown)
     * @return IoResult<IoHandle<T> > 
     */
    static auto make(IoContext &ctxt, T fd, IoDescriptor::Type type = IoDescriptor::Unknown) -> IoResult<IoHandle<T> > {
        auto desc = ctxt.addDescriptor(fd_t(fd), type);
        if (!desc) {
            return Err(desc.error());
        }
        return IoHandle<T> {
            IoDescriptor::Ptr {*desc, IoDescriptor::Deleter {&ctxt} },
            std::move(fd)
        };
    }

    static auto make(T fd, IoDescriptor::Type type = IoDescriptor::Unknown) -> IoResult<IoHandle<T> > {
        auto ctxt = IoContext::currentThread();
        if (!ctxt) {
            return Err(IoError::InvalidArgument);
        }
        return make(*ctxt, std::move(fd), type);
    }
private:
    struct Deleter {
        IoContext *ctxt = nullptr;

        auto operator()(IoDescriptor *desc) const -> void { auto _ = ctxt->removeDescriptor(desc); }
    };

    IoDescriptor::Ptr mDesc {};
    T                 mFd {};
};

/**
 * @brief Convert an IoDescriptor::Type to a string
 * 
 * @param type 
 * @return std::string_view 
 */
inline auto toString(IoDescriptor::Type type) -> std::string_view {
    using Type = IoDescriptor::Type;
    switch (type) {
        case Type::File: return "File"; 
        case Type::Socket: return "Socket"; 
        case Type::Pipe: return "Pipe"; 
        case Type::Tty: return "Tty"; 
        case Type::Unknown: return "Unknown"; 
        default: return "Unknown"; 
    }
}

#if defined(_WIN32)
namespace win32 {
    inline auto waitObject(void *handle) -> IoTask<void> {
        return IoContext::currentThread()->waitObject(handle);
    }
} // namespace win32
#endif

ILIAS_NS_END