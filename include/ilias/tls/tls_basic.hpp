#pragma once

#include <ilias/detail/scope_exit.hpp>
#include <ilias/io/dyn_traits.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/io/error.hpp>
#include <ilias/io/ext.hpp>
#include <ilias/buffer.hpp>
#include <memory> // std::unique_ptr
#include <string> // std::string_view
#include <cstdio> // fopen

ILIAS_NS_BEGIN

// Forward declarations
enum class TlsRole;
enum class TlsBackend;
enum class TlsVersion;
enum class TlsVerify;

#if defined(_WIN32)
namespace win32 {
    extern auto ILIAS_API toWide(std::string_view str) -> std::wstring;
} // namespace win32
#endif // defined(_WIN32)

// The implementation of the tls
namespace tls {

// INTERNAL, use pimpl to hide the implementation
class ILIAS_API TlsState {
public:
    struct Deleter {
        auto operator()(TlsState *ptr) -> void { ptr->destroy(); }
    };

    auto destroy() -> void;

    // Read
    auto read(StreamView stream, MutableBuffer buffer) -> IoTask<size_t>;

    // Write
    auto write(StreamView stream, Buffer buffer) -> IoTask<size_t>;
    auto flush(StreamView stream) -> IoTask<void>;
    auto shutdown(StreamView stream) -> IoTask<void>;

    // Tls
    auto handshake(StreamView stream, TlsRole role) -> IoTask<void>;
    auto setHostname(std::string_view hostname) -> void;
    auto setAlpnProtocols(std::span<const std::string_view> protocols) -> bool;
    auto alpnSelected() const -> std::string_view;

    // Native
    auto nativeHandle() const -> void *;

    static auto make(void *ctxt) -> TlsState *;
protected:
    TlsState() = default;
    ~TlsState() = default;
};

// INTERNAL!!
namespace context {
    extern auto ILIAS_API make(uint32_t flags) -> void *;
    extern auto ILIAS_API destroy(void *ctxt) -> void;
    extern auto ILIAS_API backend() -> TlsBackend;

    extern auto ILIAS_API setVerify(void *ctxt, TlsVerify verify) -> void;
    extern auto ILIAS_API setMinVersion(void *ctxt, std::optional<TlsVersion> version) -> void;
    extern auto ILIAS_API setMaxVersion(void *ctxt, std::optional<TlsVersion> version) -> void;
    extern auto ILIAS_API loadDefaultRootCerts(void *ctxt) -> bool;
    extern auto ILIAS_API loadRootCerts(void *ctxt, Buffer buffer) -> bool;
    extern auto ILIAS_API usePrivateKey(void *ctxt, Buffer key, std::string_view password) -> bool;
    extern auto ILIAS_API useCert(void *ctxt, Buffer cert) -> bool;

    struct Deleter {
        auto operator()(void *ptr) -> void { destroy(ptr); }
    };
} // namespace context

using TlsHandle = std::unique_ptr<TlsState, TlsState::Deleter>;
using TlsContextHandle = std::unique_ptr<void, context::Deleter>;

} // namespace tls

/**
 * @brief The role of the TlsStream, used for handshake
 * 
 */
enum class TlsRole {
    Client,
    Server
};

/**
 * @brief The backend used for the TlsContext
 * 
 */
enum class TlsBackend {
    Schannel,
    OpenSSL
};

/**
 * @brief The version of the tls
 * 
 */
enum class TlsVersion {
    Tlsv1_2,
    Tlsv1_3
};

/**
 * @brief The verify mode for tls
 * 
 */
enum class TlsVerify {
    None,  // No verification
    Peer,  // Verify the peer certificate
    Mutual // Verify the peer certificate and the client certificate
};

/**
 * @brief The TlsContext
 * @note By the implementation limitation (if schannel), please configure the context before creating the TlsStream
 * 
 */
class TlsContext final {
public:
    enum Flags : uint32_t {
        None               = 0,
        NoVerify           = 1 << 10, // Tell the context to don't verify the peer certificate
        NoDefaultRootCerts = 1 << 11, // Tell the context to don't load the system CA when constructed
    };

    explicit TlsContext(uint32_t flags = None) : d(tls::context::make(flags)) {}
    TlsContext(TlsContext &&) = default;

    // Verify
    /**
     * @brief Set the Verify mode
     * @note default in TlsVerify::Peer
     * 
     * @param verify The verify mode
     */
    auto setVerify(TlsVerify verify) -> void {
        return tls::context::setVerify(d.get(), verify);
    }

    // Version
    /**
     * @brief Set the minimum allowed protocol version 
     * 
     * @param version The version (nullopt on unspec)
     */
    auto setMinVersion(std::optional<TlsVersion> version) -> void {
        return tls::context::setMinVersion(d.get(), version);
    }

    /**
     * @brief Set the maximum allowed protocol version 
     * 
     * @param version The version (nullopt on unspec)
     */
    auto setMaxVersion(std::optional<TlsVersion> version) -> void {
        return tls::context::setMaxVersion(d.get(), version);
    }

    // Roots certificates
    auto loadDefaultRootCerts() -> bool {
        return tls::context::loadDefaultRootCerts(d.get());
    }

    auto loadRootCertsFile(std::string_view path) -> bool {
        return withOpen(path, [&](Buffer buffer) {
            return loadRootCerts(buffer);
        });
    }

    auto loadRootCerts(Buffer buffer) -> bool {
        return tls::context::loadRootCerts(d.get(), buffer);
    }

    // Private key and certificate
    auto usePrivateKeyFile(std::string_view file, std::string_view password = {}) -> bool {
        return withOpen(file, [&](Buffer buffer) {
            return usePrivateKey(buffer, password);
        });
    }
    
    auto usePrivateKey(Buffer key, std::string_view password = {}) -> bool {
        return tls::context::usePrivateKey(d.get(), key, password);
    }

    // Certificate
    auto useCertFile(std::string_view file) -> bool {
        return withOpen(file, [&](Buffer buffer) {
            return useCert(buffer);
        });
    }

    auto useCert(Buffer cert) -> bool {
        return tls::context::useCert(d.get(), cert);
    }

    // Get the backend used by the context
    static auto backend() -> TlsBackend {
        return tls::context::backend();
    }
private:
    // For open file and read the content
    template <typename Fn>
    static auto withOpen(std::string_view file, Fn fn) -> bool {
        ::FILE *fp = nullptr;
#if defined(_WIN32)
        // Use wopen for unicode, and use _s version to let the compiler shut up
        auto err = ::_wfopen_s(&fp, win32::toWide(file).c_str(), L"rb");
        if (err != 0) {
            return false;
        }
#else
        fp = ::fopen(std::string(file).c_str(), "rb");
#endif // defined(_WIN32)
        if (!fp) {
            return false;
        }
        ScopeExit _([fp]() {
            ::fclose(fp);
        });

        ::fseek(fp, 0, SEEK_END);
        auto size = ::ftell(fp);
        if (size <=0) {
            return false;
        }
        ::fseek(fp, 0, SEEK_SET);

        auto ptr = std::make_unique<std::byte []>(size);
        auto n = ::fread(ptr.get(), 1, size, fp);
        if (n != size) {
            return false;
        }
        return fn(std::span{ptr.get(), static_cast<size_t>(n)});
    }

    tls::TlsContextHandle d;
template <Stream T>
friend class TlsStream;
};

/**
 * @brief The TlsStream
 * 
 * @tparam T 
 */
template <Stream T>
class TlsStream final : public StreamExt<TlsStream<T> > {    
public:
    explicit TlsStream(TlsContext &ctxt, T stream) : mHandle(tls::TlsState::make(ctxt.d.get())), mStream(std::move(stream)) {}
    TlsStream(TlsStream &&other) = default;
    TlsStream() = default;

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t> {
        return mHandle->read(mStream, buffer);
    }

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t> {
        return mHandle->write(mStream, buffer);
    }

    auto flush() -> IoTask<void> {
        return mHandle->flush(mStream);
    }

    auto shutdown() -> IoTask<void> {
        return mHandle->shutdown(mStream);
    }

    // Tls specific
    // User should call handshake before use any read / write
    auto handshake(TlsRole role) -> IoTask<void> {
        return mHandle->handshake(mStream, role);
    }

    auto setHostname(std::string_view hostname) -> void {
        return mHandle->setHostname(hostname);
    }

    // Try set the ALPN protocols, return false on unsupported
    auto setAlpnProtocols(std::span<const std::string_view> protocols) -> bool {
        return mHandle->setAlpnProtocols(protocols);
    }

    // Get the selected ALPN protocol
    auto alpnSelected() -> std::string_view {
        return mHandle->alpnSelected();
    }

    // Wrapper specific
    auto nextLayer() -> T & { return mStream; }

    // Detach the stream in it, all pending data will lost
    auto detach() -> T {
        mHandle.reset();
        return std::move(mStream);
    }

    auto operator =(const TlsStream &other) -> TlsStream & = delete;
    auto operator =(TlsStream &&other) -> TlsStream & = default;

    // Check the stream is valid
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    tls::TlsHandle mHandle;
    T              mStream;
};

// Formatter to enum
inline auto toString(TlsBackend backend) -> std::string_view {
    switch (backend) {
        case TlsBackend::OpenSSL: return "OpenSSL";
        case TlsBackend::Schannel: return "Schannel";
        default: return "Unknown";
    }
}

inline auto toString(TlsVersion version) -> std::string_view {
    switch (version) {
        case TlsVersion::Tlsv1_2: return "Tlsv1.2";
        case TlsVersion::Tlsv1_3: return "Tlsv1.3";
    }   return "Unknown";
}

// Mark it
ILIAS_FORMATTABLE(TlsBackend);
ILIAS_FORMATTABLE(TlsVersion);

ILIAS_NS_END