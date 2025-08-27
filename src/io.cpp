#include <ilias/io/system_error.hpp>
#include <ilias/io/duplex.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/io/error.hpp>
#include <array>
#include <tuple>

ILIAS_NS_BEGIN

/**
 * @brief The utility namespace for reflection
 * 
 */
namespace reflect {

/**
 * @brief Get the name of the type in compile time
 * 
 * @tparam T 
 * @return std::string_view 
 */
template <auto T>
consteval auto nameof() {
#ifdef _MSC_VER
    constexpr std::string_view name(__FUNCSIG__);
    constexpr size_t nsEnd = name.find_last_of("::");
    constexpr size_t end = name.find('>', nsEnd);
    // size_t dotBegin = name.find_first_of(',');
    // size_t end = name.find_last_of('>');
    // return name.substr(dotBegin + 1, end - dotBegin - 1);
    return name.substr(nsEnd + 1, end - nsEnd - 1);
#else
    std::string_view name(__PRETTY_FUNCTION__);
    size_t eqBegin = name.find_last_of(' ');
    size_t end = name.find_last_of(']');
    return name.substr(eqBegin + 1, end - eqBegin - 1);
#endif
}

/**
 * @brief Get the array of the name of the type in compile time
 * 
 * @tparam T 
 * @return std::array<char,?> (no null terminator)
 */
template <auto T>
consteval auto nameof2() {
    constexpr auto name = nameof<T>();
    std::array<char, name.size()> buffer {0};
    for (size_t i = 0; i < name.size(); ++i) {
        buffer[i] = name[i];
    }
    return buffer;
}

template <size_t ...N, typename T>
auto enum2str(std::index_sequence<N...>, T i) -> std::string_view {
    constexpr static auto data = std::tuple {
        reflect::nameof2<T(N)>()...
    };
    constexpr std::array<std::string_view, sizeof...(N)> table {
        std::string_view(
            std::get<N>(data).data(),
            std::get<N>(data).size()
        )...
    };
    auto idx = static_cast<int64_t>(i);
    if (idx < 0 || idx >= int64_t(table.size())) {
        return "Unknown";
    }
    return table[idx];
}

} // namespace reflect

#pragma region IoError
// Io
auto IoCategory::message(int err) const -> std::string {
    return IoError(err).toString();
}

auto IoCategory::name() const noexcept -> const char * {
    return "io";
}

auto IoCategory::instance() noexcept -> const IoCategory & {
    static constinit IoCategory instance;
    return instance;
}

auto IoCategory::equivalent(int value, const std::error_condition &other) const noexcept -> bool {
    if (other.category() == instance()) { // Compare with self
        return value == other.value();
    }
    if (other.category() == std::generic_category()) { // Compare with std::errc
        return IoError(value).toStd() == std::errc(other.value());
    }
    return false;
}

auto IoError::toString() const -> std::string {
    auto view = reflect::enum2str(std::make_index_sequence<IoError::Other>(), mErr);
    return std::string(view);
}

auto IoError::toStd() const -> std::errc {
    switch (mErr) {
        case IoError::Ok                        : return std::errc();
        case IoError::AccessDenied              : return std::errc::permission_denied;
        case IoError::AddressInUse              : return std::errc::address_in_use;
        case IoError::AddressNotAvailable       : return std::errc::address_not_available;
        case IoError::AddressFamilyNotSupported : return std::errc::address_family_not_supported;
        case IoError::AlreadyInProgress         : return std::errc::operation_in_progress;
        case IoError::BadFileDescriptor         : return std::errc::bad_file_descriptor;
        case IoError::ConnectionAborted         : return std::errc::connection_aborted;
        case IoError::ConnectionRefused         : return std::errc::connection_refused;
        case IoError::ConnectionReset           : return std::errc::connection_reset;
        case IoError::DestinationAddressRequired: return std::errc::destination_address_required;
        case IoError::BadAddress                : return std::errc::bad_address;
        case IoError::HostDown                  : return std::errc::host_unreachable;
        case IoError::HostUnreachable           : return std::errc::host_unreachable;
        case IoError::InProgress                : return std::errc::operation_in_progress;
        case IoError::InvalidArgument           : return std::errc::invalid_argument;
        case IoError::SocketIsConnected         : return std::errc::already_connected;
        case IoError::TooManyOpenFiles          : return std::errc::too_many_files_open;
        case IoError::MessageTooLarge           : return std::errc::message_size;
        case IoError::NetworkDown               : return std::errc::network_down;
        case IoError::NetworkReset              : return std::errc::network_reset;
        case IoError::NetworkUnreachable        : return std::errc::network_unreachable;
        case IoError::NoBufferSpaceAvailable    : return std::errc::no_buffer_space;
        case IoError::ProtocolOptionNotSupported: return std::errc::no_protocol_option;
        case IoError::SocketIsNotConnected      : return std::errc::not_connected;
        case IoError::NotASocket                : return std::errc::not_a_socket;
        case IoError::OperationNotSupported     : return std::errc::operation_not_supported;
        case IoError::ProtocolNotSupported      : return std::errc::protocol_not_supported;
        case IoError::TimedOut                  : return std::errc::timed_out;
        case IoError::WouldBlock                : return std::errc::operation_would_block;
        case IoError::Canceled                  : return std::errc::operation_canceled;
        default                                 : return std::errc::io_error;
    }
}

#pragma region SystemError
// System
auto SystemCategory::message(int err) const -> std::string {
    return SystemError(err).toString();
}

auto SystemCategory::name() const noexcept -> const char * {
    return "os";
}

auto SystemCategory::default_error_condition(int err) const noexcept -> std::error_condition {
    return SystemError(err).toIoError().toStd();
}

auto SystemCategory::equivalent(int value, const std::error_condition &other) const noexcept -> bool {
    if (other.category() == instance()) {
        return value == other.value();
    }
    if (other.category() == IoCategory::instance()) { // IoError
        return SystemError(value).toIoError() == IoError(other.value());
    }
    if (other.category() == std::generic_category()) { // std::errc
        return SystemError(value).toIoError().toStd() == std::errc(other.value());
    }
    return false;
}

auto SystemCategory::instance() noexcept -> const SystemCategory & {
    static constinit SystemCategory instance;
    return instance;
}

auto SystemError::toString() const -> std::string {

#ifdef _WIN32
    wchar_t *args = nullptr;
    ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, mErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&args), 0, nullptr
    );
    auto len = ::WideCharToMultiByte(CP_UTF8, 0, args, -1, nullptr, 0, nullptr, nullptr);
    std::string buf(len - 1, '\0'); //< This len includes the null terminator
    len = ::WideCharToMultiByte(CP_UTF8, 0, args, -1, &buf[0], len, nullptr, nullptr);
    ::LocalFree(args);
    return buf;
#else
    return ::strerror(mErr);
#endif

}

auto SystemError::toIoError() const -> IoError {
    switch (mErr) {
        case SystemError::Ok                        : return IoError::Ok;
        case SystemError::AccessDenied              : return IoError::AccessDenied;
        case SystemError::AddressInUse              : return IoError::AddressInUse;
        case SystemError::AddressNotAvailable       : return IoError::AddressNotAvailable;
        case SystemError::AddressFamilyNotSupported : return IoError::AddressFamilyNotSupported;
        case SystemError::AlreadyInProgress         : return IoError::AlreadyInProgress;
        case SystemError::BadFileDescriptor         : return IoError::BadFileDescriptor;
        case SystemError::ConnectionAborted         : return IoError::ConnectionAborted;
        case SystemError::ConnectionRefused         : return IoError::ConnectionRefused;
        case SystemError::ConnectionReset           : return IoError::ConnectionReset;
        case SystemError::DestinationAddressRequired: return IoError::DestinationAddressRequired;
        case SystemError::BadAddress                : return IoError::BadAddress;
        case SystemError::HostDown                  : return IoError::HostDown;
        case SystemError::HostUnreachable           : return IoError::HostUnreachable;
        case SystemError::InProgress                : return IoError::InProgress;
        case SystemError::InvalidArgument           : return IoError::InvalidArgument;
        case SystemError::SocketIsConnected         : return IoError::SocketIsConnected;
        case SystemError::TooManyOpenFiles          : return IoError::TooManyOpenFiles;
        case SystemError::MessageTooLarge           : return IoError::MessageTooLarge;
        case SystemError::NetworkDown               : return IoError::NetworkDown;
        case SystemError::NetworkReset              : return IoError::NetworkReset;
        case SystemError::NetworkUnreachable        : return IoError::NetworkUnreachable;
        case SystemError::NoBufferSpaceAvailable    : return IoError::NoBufferSpaceAvailable;
        case SystemError::ProtocolOptionNotSupported: return IoError::ProtocolOptionNotSupported;
        case SystemError::SocketIsNotConnected      : return IoError::SocketIsNotConnected;
        case SystemError::NotASocket                : return IoError::NotASocket;
        case SystemError::OperationNotSupported     : return IoError::OperationNotSupported;
        case SystemError::ProtocolFamilyNotSupported: return IoError::ProtocolFamilyNotSupported;
        case SystemError::ProtocolNotSupported      : return IoError::ProtocolNotSupported;
        case SystemError::SocketShutdown            : return IoError::SocketShutdown;
        case SystemError::SocketTypeNotSupported    : return IoError::SocketTypeNotSupported;
        case SystemError::TimedOut                  : return IoError::TimedOut;
        case SystemError::WouldBlock                : return IoError::WouldBlock;
        case SystemError::Canceled                  : return IoError::Canceled;
        default                                     : return IoError::Other;
    }
}

#pragma region DuplexStream

struct ByteChannel {
    StreamBuffer buffer;
    runtime::CoroHandle reader; // suspend on read
    runtime::CoroHandle writer; // suspend on write
    size_t maxSize = 0;
    bool readerClose = false;
    bool writerClose = false;
};

struct DuplexStream::Impl {
    ByteChannel read;
    ByteChannel write;
};

auto DuplexStream::make(size_t size) -> std::pair<DuplexStream, DuplexStream> {
    if (size == 0) {
        ILIAS_THROW(std::invalid_argument("Size must be greater than 0"));
    }
    auto impl = std::make_shared<Impl>();
    impl->read.maxSize = size;
    impl->write.maxSize = size;

    return std::pair(
        DuplexStream(impl, false), // We use bool flip, to make aother stream write on read buffer and read on write buffer
        DuplexStream(impl, true)
    );
}

namespace {
    auto wakeupIf(runtime::CoroHandle &handle) -> void {
        if (handle) {
            handle.schedule();
            handle = nullptr;
        }
    }

    auto shutdownImpl(DuplexStream::Impl *d, bool flip) -> void {
        auto &readChan = flip ? d->write : d->read;
        auto &writeChan = flip ? d->read : d->write;

        readChan.readerClose = true;
        writeChan.writerClose = true;
        wakeupIf(readChan.writer);
        wakeupIf(writeChan.reader);
    }
}

auto DuplexStream::read(MutableBuffer buffer) -> IoTask<size_t> {
    struct Awaiter {
        auto await_ready() -> bool {
            return !chan.buffer.empty() || chan.writerClose; // Has data or no-none will write
        }
        auto await_suspend(runtime::CoroHandle h) -> void {
            chan.reader = h;
            handle = h;
            reg.register_<&Awaiter::onStopRequested>(h.stopToken(), this);
        }
        auto await_resume() {
            return chan.buffer.data();
        }
        auto onStopRequested() -> void {
            if (!chan.reader) { // We are scheduled or already stopped
                return;
            }
            chan.reader = nullptr;
            handle.setStopped();
        }

        ByteChannel &chan;
        runtime::CoroHandle handle;
        runtime::StopRegistration reg;
    };

    auto &chan = mFlip ? d->write : d->read;

    // Read data here
    auto span = co_await Awaiter{ .chan = chan };
    auto left = std::min(span.size(), buffer.size());
    ::memcpy(buffer.data(), span.data(), left);
    chan.buffer.consume(left);

    // Wakeup writer if exists
    wakeupIf(chan.writer);
    co_return left;
}

auto DuplexStream::write(Buffer buffer) -> IoTask<size_t> {
    struct Awaiter {
        auto await_ready() -> bool {
            return chan.buffer.size() < chan.maxSize || chan.readerClose; // Has space or no-one will read
        }
        auto await_suspend(runtime::CoroHandle h) -> void {
            chan.writer = h;
            handle = h;
            reg.register_<&Awaiter::onStopRequested>(h.stopToken(), this);
        }
        auto await_resume() {}
        auto onStopRequested() -> void {
            if (!chan.writer) { // We are scheduled or already stopped
                return;
            }
            chan.writer = nullptr;
            handle.setStopped();
        }

        ByteChannel &chan;
        runtime::CoroHandle handle;
        runtime::StopRegistration reg;
    };

    auto &chan = mFlip ? d->read : d->write;    
    co_await Awaiter{ .chan = chan };

    // Write data here
    if (chan.readerClose) {
        co_return 0; // EOF, no data written
    }
    auto left = std::min(buffer.size(), chan.maxSize - chan.buffer.size());
    auto span = chan.buffer.prepare(left);
    ::memcpy(span.data(), buffer.data(), left);
    chan.buffer.commit(left);

    // Wakeup reader if exists
    wakeupIf(chan.reader);
    co_return left;
}

auto DuplexStream::shutdown() -> IoTask<void> {
    shutdownImpl(d.get(), mFlip);
    co_return {};
}

auto DuplexStream::flush() -> IoTask<void> {
    co_return {}; // no-op
}

auto DuplexStream::close() -> void {
    if (d) {
        shutdownImpl(d.get(), mFlip);
    }
    d.reset();
}

ILIAS_NS_END