#include <ilias/io/system_error.hpp>
#include <ilias/io/error.hpp>

ILIAS_NS_BEGIN

auto SystemCategory::message(int err) const -> std::string {
    return SystemError(err).toString();
}

auto SystemCategory::name() const noexcept -> const char * {
    return "os";
}

auto SystemCategory::default_error_condition(int err) const noexcept -> std::error_condition {
    auto ec = SystemError(err).translate();
    return ec;
}

auto SystemCategory::instance() -> SystemCategory & {
    static SystemCategory instance;
    return instance;
}

auto SystemError::translate() const -> std::errc {
    switch (mErr) {
        case SystemError::Ok                        : return std::errc();
        case SystemError::AccessDenied              : return std::errc::permission_denied;
        case SystemError::AddressInUse              : return std::errc::address_in_use;
        case SystemError::AddressNotAvailable       : return std::errc::address_not_available;
        case SystemError::AddressFamilyNotSupported : return std::errc::address_family_not_supported;
        case SystemError::AlreadyInProgress         : return std::errc::operation_in_progress;
        case SystemError::BadFileDescriptor         : return std::errc::bad_file_descriptor;
        case SystemError::ConnectionAborted         : return std::errc::connection_aborted;
        case SystemError::ConnectionRefused         : return std::errc::connection_refused;
        case SystemError::ConnectionReset           : return std::errc::connection_reset;
        case SystemError::DestinationAddressRequired: return std::errc::destination_address_required;
        case SystemError::BadAddress                : return std::errc::invalid_argument;
        case SystemError::HostDown                  : return std::errc::host_unreachable;
        case SystemError::HostUnreachable           : return std::errc::host_unreachable;
        case SystemError::InProgress                : return std::errc::operation_in_progress;
        case SystemError::InvalidArgument           : return std::errc::invalid_argument;
        case SystemError::SocketIsConnected         : return std::errc::already_connected;
        case SystemError::TooManyOpenFiles          : return std::errc::too_many_files_open;
        case SystemError::MessageTooLarge           : return std::errc::message_size;
        case SystemError::NetworkDown               : return std::errc::network_down;
        case SystemError::NetworkReset              : return std::errc::network_reset;
        case SystemError::NetworkUnreachable        : return std::errc::network_unreachable;
        case SystemError::NoBufferSpaceAvailable    : return std::errc::no_buffer_space;
        case SystemError::ProtocolOptionNotSupported: return std::errc::no_protocol_option;
        case SystemError::SocketIsNotConnected      : return std::errc::not_connected;
        case SystemError::NotASocket                : return std::errc::not_a_socket;
        case SystemError::OperationNotSupported     : return std::errc::operation_not_supported;
        case SystemError::ProtocolNotSupported      : return std::errc::protocol_not_supported;
        case SystemError::TimedOut                  : return std::errc::timed_out;
        case SystemError::WouldBlock                : return std::errc::operation_would_block;
        case SystemError::Canceled                  : return std::errc::operation_canceled;
        default                                     : return std::errc::io_error;
    }
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


ILIAS_NS_END