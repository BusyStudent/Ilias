#pragma once

#include <ilias/io/dyn_traits.hpp>
#include <ilias/io/method.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/io/error.hpp>
#include <ilias/buffer.hpp>
#include <memory> // std::unique_ptr

ILIAS_NS_BEGIN

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
    auto handshake(StreamView stream) -> IoTask<void>;
    auto setHostname(std::string_view hostname) -> void;
    auto setAlpnProtocols(std::span<const std::string_view> protocols) -> bool;
    auto alpnSelected() const -> std::string_view;

    static auto make(void *ctxt, bool isServer) -> TlsState *;
protected:
    TlsState() = default;
    ~TlsState() = default;
};

// INTERNAL!!
namespace context {
    extern auto ILIAS_API make() -> void *;
    extern auto ILIAS_API destroy(void *) -> void;
} // namespace context

using TlsHandle = std::unique_ptr<TlsState, TlsState::Deleter>;

} // namespace tls

/**
 * @brief The TlsContext
 * 
 */
class TlsContext {
public:
    TlsContext() : d(tls::context::make()) {}
    TlsContext(const TlsContext &) = delete;
    TlsContext(TlsContext &&) = default;
    ~TlsContext() = default;
private:
    struct Deleter {
        auto operator()(void *ptr) -> void { tls::context::destroy(ptr); }
    };

    std::unique_ptr<void, Deleter> d;
template <Stream T>
friend class TlsStream;
};

/**
 * @brief The TlsStream
 * 
 * @tparam T 
 */
template <Stream T>
class TlsStream final : public StreamMethod<TlsStream<T> > {    
public:
    TlsStream() = default;
    TlsStream(TlsStream &&other) = default;
    TlsStream(TlsContext &ctxt, T &&stream) : mHandle(tls::TlsState::make(ctxt.d.get(), false)), mStream(std::move(stream)) {}
    ~TlsStream() = default;

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
    auto handshake() -> IoTask<void> {
        return mHandle->handshake(mStream);
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

    // Detach the stream in it, all data will lost
    auto detach() -> T {
        mHandle.reset();
        return std::move(mStream);
    }

    auto operator =(const TlsStream &other) -> TlsStream & = delete;
    auto operator =(TlsStream &&other) -> TlsStream & = default;

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    tls::TlsHandle mHandle;
    T              mStream;
};

ILIAS_NS_END