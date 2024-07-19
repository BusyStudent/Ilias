#pragma once

#include "../net/traits.hpp"
#include "../detail/ring.hpp"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <memory> //< for std::make_unique<T []>


ILIAS_NS_BEGIN

namespace OpenSsl {

/**
 * 
 * @brief Ssl Context
 * 
 */
class SslContext {
public:
    SslContext() {
        SSL_library_init();

        mCtxt = SSL_CTX_new(TLS_method());
        SSL_CTX_set_mode(mCtxt, SSL_MODE_AUTO_RETRY);
    }
    SslContext(const SslContext &) = delete;
    ~SslContext() {
        SSL_CTX_free(mCtxt);
    }

    SSL_CTX *get() const noexcept {
        return mCtxt;
    }
private:
    SSL_CTX *mCtxt = nullptr;
};

/**
 * @brief The mem bio based on ring buffer
 * 
 */
class SslBio {
public:
    SslBio() {
        static Method method = _register();
        mBio = BIO_new(method.method);
        BIO_set_data(mBio, this);
        BIO_set_init(mBio, 1);
        BIO_set_shutdown(mBio, 0);
    }
    SslBio(const SslBio &) = delete;
    ~SslBio() {

    }

    struct Method {
        Method(BIO_METHOD *method) : method(method) { }
        Method(const Method &) = delete;
        ~Method() {
            BIO_meth_free(method);
        }
        BIO_METHOD *method;
    };
    static auto _register() -> BIO_METHOD * {
        BIO_METHOD *method = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "IliasSslBio");
        BIO_meth_set_write_ex(method, [](BIO *b, const char *data, size_t len, size_t *ret) {
            return static_cast<SslBio*>(BIO_get_data(b))->_write(data, len, ret);
        });
        BIO_meth_set_read_ex(method, [](BIO *b, char *data, size_t len, size_t *ret) {
            return static_cast<SslBio*>(BIO_get_data(b))->_read(data, len, ret);
        });
        BIO_meth_set_ctrl(method, [](BIO *b, int cmd, long num, void *ptr) {
            return static_cast<SslBio*>(BIO_get_data(b))->_ctrl(cmd, num, ptr);
        });
        return method;
    }
    auto _write(const char *data, size_t len, size_t *ret) -> int {
        if (!data) {
            return 0;
        }
        BIO_clear_retry_flags(mBio);
        if (mWriteRing.full()) {
            BIO_set_retry_write(mBio);
            return 0;
        }
        *ret = mWriteRing.push(std::as_bytes(std::span(data, len)));
        return 1;
    }
    auto _read(char *data, size_t len, size_t *ret) -> int {
        if (!data) {
            return 0;
        }
        BIO_clear_retry_flags(mBio);
        if (mReadRing.empty()) {
            BIO_set_retry_read(mBio);
            return 0;
        }
        *ret = mReadRing.pop(std::as_writable_bytes(std::span(data, len)));
        return 1;
    }
    auto _ctrl(int cmd, long num, void *ptr) -> long {
        switch (cmd) {
            case BIO_CTRL_FLUSH: mFlush = true; return 1;
        }
        return 0; 
    }

    BIO *mBio = nullptr;
    RingBuffer<1024 * 8> mReadRing;
    RingBuffer<1024 * 8> mWriteRing;
    bool mFlush = false;
};
/**
 * @brief Wrapp T into BIO
 * 
 * @tparam T 
 */
template <typename T>
class SslWrap final : public SslBio {
public:
    SslWrap(T &&f) : mFd(std::move(f)) { }
    T mFd;
};

/**
 * @brief Generic Ssl Class
 * 
 * @tparam T 
 */
template <typename T>
class SslSocket {
public:
    SslSocket() = default;
    SslSocket(SSL_CTX *ctxt, T &&f) {
        mBio = new SslWrap<T>(std::move(f));
        mSsl = SSL_new(ctxt);
        mCtxt = ctxt;
        SSL_set_bio(mSsl, mBio->mBio, mBio->mBio);
        SSL_set_mode(mSsl, SSL_MODE_AUTO_RETRY);
    }
    SslSocket(SslContext &ctxt, T &&f) : SslSocket(ctxt.get(), std::move(f)) { }
    SslSocket(SslSocket &&sock) {
        mBio = sock.mBio;
        sock.mBio = nullptr;

        mSsl = sock.mSsl;
        sock.mSsl = nullptr;
    }
    ~SslSocket() {
        SSL_free(mSsl);
        delete mBio;
    }
    /**
     * @brief Get local endpoint
     * 
     * @return IPEndpoint 
     */
    auto localEndpoint() const -> IPEndpoint {
        return mBio->mFd.localEndpoint();
    }
protected:
#if defined(__cpp_impl_coroutine)
    auto _handleError(int errcode) -> Task<void> {
        if (errcode == SSL_ERROR_WANT_READ) {
            auto ret = co_await _waitReadable();
            if (!ret) {
                co_return Unexpected(ret.error());
            }
        }
        else if (errcode == SSL_ERROR_WANT_WRITE) {
            auto ret = co_await _flushWrite();
            if (!ret) {
                co_return Unexpected(ret.error());
            }
        }
        else if (errcode == SSL_ERROR_SSL) {
            // We should look for the error queue
            auto err = ERR_peek_error();
#if !defined(NDEBUG)
            ERR_print_errors_fp(stderr);
#endif
            ERR_clear_error();
            co_return Unexpected(Error::SSL);
        }
        else {
            // Error
            co_return Unexpected(Error::SSLUnknown);
        }
        co_return Result<>();
    }
    auto _flushWrite() -> Task<void> {
        // Flush the data
        size_t dataLeft = mBio->mWriteRing.size();
        auto tmpbuffer = std::make_unique<std::byte[]>(dataLeft);
        mBio->mWriteRing.pop(tmpbuffer.get(), dataLeft);

        auto current = tmpbuffer.get();
        while (dataLeft > 0) {
            // FIXME: if , it will lost data, we should add peek and discard method in ringbuffer
            auto ret = co_await mBio->mFd.send(current, dataLeft);
            if (!ret) {
                co_return Unexpected(ret.error()); //< Send Error
            }
            dataLeft -= *ret;
            current += *ret;
        }
        co_return Result<>();
    }
    auto _waitReadable() -> Task<void> {
        if (!mBio->mWriteRing.empty()) {
            // Require flush
            auto ret = co_await _flushWrite();
            if (!ret) {
                co_return ret;
            }
        }

        size_t ringLeft = mBio->mReadRing.capacity() - mBio->mReadRing.size();
        auto tmpbuffer = std::make_unique<std::byte[]>(ringLeft);
        auto ret = co_await mBio->mFd.recv(tmpbuffer.get(), ringLeft);
        if (!ret) {
            co_return Unexpected(ret.error()); //< Read Error
        }
        auto written = mBio->mReadRing.push(tmpbuffer.get(), *ret);
        ILIAS_ASSERT(written == *ret);
        co_return Result<>();
    }
    auto _accept() -> Task<void> {
        while (true) {
            int sslAccept = SSL_accept(mSsl);
            if (sslAccept == 1) {
                co_return Result<>();
            }
            int errcode = SSL_get_error(mSsl, sslAccept);
            if (auto ret = co_await this->_handleError(errcode); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
#endif

    SSL *mSsl = nullptr;
    SslWrap<T> *mBio = nullptr;
    SSL_CTX *mCtxt = nullptr;
};

/**
 * @brief Client Ssl Class, require StreamClient as T
 * 
 * @tparam T 
 */
template <StreamClient T = IStreamClient>
class SslClient : public SslSocket<T> {
public:
    SslClient() = default;
    SslClient(const SslClient &) = delete;
    SslClient(SslClient &&) = default;
    SslClient(SslContext &ctxt, T &&f) : SslSocket<T>(ctxt.get(), std::move(f)) {
        SSL_set_connect_state(this->mSsl);
    }
    SslClient(SSL_CTX *ctxt, T &&f) : SslSocket<T>(ctxt, std::move(f)) {
        SSL_set_connect_state(this->mSsl);
    }
    /**
     * @brief Set the Hostname object (for SNI)
     * 
     * @param hostname The 0 terminated string containing the hostname
     * @return true 
     * @return false 
     */
    auto setHostname(const char *hostname) -> bool {
        return SSL_set_tlsext_host_name(this->mSsl, hostname);
    }
    auto setHostname(const std::string &hostname) -> bool {
        return SSL_set_tlsext_host_name(this->mSsl, hostname.c_str());
    }
    auto setHostname(std::string_view hostname) -> bool {
        return SSL_set_tlsext_host_name(this->mSsl, std::string(hostname).c_str());
    }
    /**
     * @brief Set the Alpn Proto object
     * 
     * @param container The container of the protocols, in normal format (like "http/1.1")
     * @return true 
     * @return false 
     */
    template <typename U>
    auto setAlpn(U &&container) -> bool {
        std::string buf;
        for (const auto &i : container) {
            auto view = std::string_view(i);
            buf.push_back(char(view.size()));
            buf.append(view);
        }
        return SSL_set_alpn_protos(this->mSsl, reinterpret_cast<const uint8_t *>(buf.data()), buf.size()) == 0;
    }
    /**
     * @brief Get the selected ALPN Protocol
     * 
     * @return std::string_view (empty on no-alpn or else) like "http/1.1"
     */
    auto alpnSelected() const -> std::string_view {
        const uint8_t *ptr = nullptr;
        unsigned int len;
        SSL_get0_alpn_selected(this->mSsl, &ptr, &len);
        return std::string_view(reinterpret_cast<const char*>(ptr), len);
    }

    /**
     * @brief Get remote Endpoint
     * 
     * @return IPEndpoint 
     */
    auto remoteEndpoint() const -> Result<IPEndpoint> {
        return this->mBio->mFd.remoteEndpoint();
    }

#if defined(__cpp_impl_coroutine)
    auto connect(const IPEndpoint &endpoint) -> Task<void> {
        auto ret = co_await this->mBio->mFd.connect(endpoint);
        if (!ret) {
            co_return ret;
        }
        co_return co_await handshake();
    }
    auto recv(void *buffer, size_t n) -> Task<size_t> {
        while (true) {
            size_t readed = 0;
            int readret = SSL_read_ex(this->mSsl, buffer, n, &readed);
            if (readret == 1) {
                co_return readed;
            }
            int errcode = 0;
            errcode = SSL_get_error(this->mSsl, readret);
            if (errcode == SSL_ERROR_ZERO_RETURN) {
                co_return 0;
            }
            if (auto ret = co_await this->_handleError(errcode); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
    auto send(const void *buffer, size_t n) -> Task<size_t> {
        while (true) {
            size_t written = 0;
            int writret = SSL_write_ex(this->mSsl, buffer, n, &written);
            if (writret == 1) {
                co_return written;
            }
            int errcode = 0;
            errcode = SSL_get_error(this->mSsl, writret);
            if (errcode == SSL_ERROR_ZERO_RETURN) {
                co_return 0;
            }
            if (auto ret = co_await this->_handleError(errcode); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
    auto shutdown() -> Task<> {
        while (true) {
            int ret = SSL_shutdown(this->mSsl);
            if (ret == 1) {
                co_return Result<>();
            }
            int errcode = 0;
            errcode = SSL_get_error(this->mSsl, ret);
            if (auto ret = co_await this->_handleError(errcode); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
    auto handshake() -> Task<> {
        while (true) {
            int sslCon = SSL_connect(this->mSsl);
            if (sslCon == 1) {
                co_return Result<>();
            }
            int errcode = SSL_get_error(this->mSsl, sslCon);
            if (auto ret = co_await this->_handleError(errcode); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
#endif
};

/**
 * @brief Listener Ssl class, require stream listener as T
 * 
 * @tparam T 
 */
template <StreamListener T = IStreamListener>
class SslListener : public SslSocket<T> {
public:
    using RawClient = typename T::Client;
    using Client    = SslClient<RawClient>;

    SslListener() = default;
    SslListener(SslListener &&) = default;
    SslListener(SslContext &ctxt, T &&f) : SslSocket<T>(ctxt, std::move(f)) {
        SSL_set_accept_state(this->mSsl);
    }

#if defined(__cpp_impl_coroutine)
    auto accept() -> Task<std::pair<Client, IPEndpoint> > {
        // TODO 
        auto val = co_await this->mBio->mFd.accept();
        if (!val) {
            co_return Unexpected(val.error());            
        }
        auto &[rawClient, addr] = *val;
        Client client(this->mCtxt, std::move(rawClient));
        if (auto ret = co_await client._accept(); !ret) {
            co_return Unexpected(ret.error());
        }
        co_return std::pair{std::move(client), addr};
    }
#endif
};

}

// Export to global if we decide use it
#if defined(ILIAS_SSL_USE_OPENSSL)
using OpenSsl::SslContext;
using OpenSsl::SslClient;
using OpenSsl::SslListener;
#endif

ILIAS_NS_END