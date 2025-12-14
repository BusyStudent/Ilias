#include <ilias/io/system_error.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/tls.hpp>
#include <mutex> // std::once_flag

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

ILIAS_NS_BEGIN

namespace tls {
namespace openssl {

// Internal BIO Method
static constinit BIO_METHOD *bioMethod = nullptr;

#pragma region OpenSSL
class TlsStateImpl : public TlsState {
public:
	// OpenSSL State
	BIO *mBio = nullptr;
	SSL *mSsl = nullptr;

	// Stream State
	bool mFlush = false; // Require a flush ?
	bool mFail = false; // Not recoverable SSL Fail

    // Buffer
    FixedStreamBuffer<16384 + 100> mReadBuffer;  //< The incoming buffer 2 ** 14 (MAX TLS SIZE) + header + trailer
    FixedStreamBuffer<16384 + 100> mWriteBuffer;

	TlsStateImpl(SSL_CTX *ctxt) : mBio(BIO_new(bioMethod)), mSsl(SSL_new(ctxt)) {
		// Bind bio to self
        BIO_set_data(mBio, this);
        BIO_set_init(mBio, 1);
        BIO_set_shutdown(mBio, 0);

		// Bind ssl to bio
		SSL_set_bio(mSsl, mBio, mBio);
		SSL_set_mode(mSsl, SSL_get_mode(mSsl) | SSL_MODE_AUTO_RETRY);
	}

	~TlsStateImpl() {
		SSL_free(mSsl);
	}

	// Callback from openssl
	auto bioRead(char *data, size_t len, size_t *ret) -> int {
        if (!data) {
            return 0;
        }
        BIO_clear_retry_flags(mBio);
        auto span = mReadBuffer.data();
        if (span.empty()) {
            BIO_set_retry_read(mBio);
            return 0;
        }
        len = std::min(len, span.size());
        std::memcpy(data, span.data(), len);
        mReadBuffer.consume(len);
        *ret = len;
        return 1;
	}

	auto bioWrite(const char *data, size_t len, size_t *ret) -> int {
        if (!data) {
            return 0;
        }
        BIO_clear_retry_flags(mBio);
        auto span = mWriteBuffer.prepare(len);
        if (span.empty()) {
            BIO_set_retry_write(mBio);
            return 0;
        }
        ::memcpy(span.data(), data, len);
        mWriteBuffer.commit(len);
        *ret = len;
        return 1;
	}

	auto bioCtrl(int cmd, [[maybe_unused]] long num, [[maybe_unused]] void *ptr) -> long {
        switch (cmd) {
            case BIO_CTRL_FLUSH: mFlush = true; return 1;
        }
        return 0;
	}

	// Our method
	auto handleError(StreamView stream, int err) -> IoTask<void> {
		switch (err) {
		case SSL_ERROR_WANT_READ: {
			if (mFlush) {
				if (auto val = co_await flushImpl(stream); !val) {
				    co_return Err(val.error());
				}
			}
			auto left = mReadBuffer.capacity() - mReadBuffer.size();
			auto data = mReadBuffer.prepare(left);
			auto n = co_await stream.read(data);
			if (!n) {
				co_return Err(n.error());
			}
			if (n == 0) {
				co_return Err(IoError::UnexpectedEOF);
			}
			mReadBuffer.commit(*n);
			break;
		}
		case SSL_ERROR_WANT_WRITE: {
			if (auto val = co_await flushImpl(stream); !val) {
				co_return Err(val.error());
			}
			break;
		}
		case SSL_ERROR_SSL: {
			// Tls Error
			mFail = true; // In documentation it says that this error is not recoverable
			co_return Err(IoError::Tls);
		}
		default: {
			// Unknown Error
			co_return Err(IoError::Tls);
		}
		}
		co_return {};
	}
	
	auto handshakeImpl(StreamView stream, TlsRole role) -> IoTask<void> {
		if (role == TlsRole::Client) {
			SSL_set_connect_state(mSsl);
		}
		else {
			SSL_set_accept_state(mSsl);
		}
		while (true) {
			int ret = SSL_do_handshake(mSsl);
			if (ret == 1) {
				break;
			}
			int err = SSL_get_error(mSsl, ret);
			if (auto res = co_await handleError(stream, err); !res) {
				co_return Err(res.error());
			}
		}
		co_return {};
	}

	// Read
	auto readImpl(StreamView stream, MutableBuffer buffer) -> IoTask<size_t> {
		size_t readed = 0;
		while (true) {
			auto ret = SSL_read_ex(mSsl, buffer.data(), buffer.size(), &readed);
			if (ret == 1) {
				break;
			}
			int err = SSL_get_error(mSsl, ret);
			if (err == SSL_ERROR_ZERO_RETURN) {
				ILIAS_DEBUG("OpenSSL", "Tls Stream: EOF");
				break; // EOF
			}
			if (auto res = co_await handleError(stream, err); !res) {
				co_return Err(res.error());
			}
		}
		co_return readed;
	}

	// Write
	auto writeImpl(StreamView stream, Buffer buffer) -> IoTask<size_t> {
		size_t written = 0;
		while (true) {
			auto ret = SSL_write_ex(mSsl, buffer.data(), buffer.size(), &written);
			if (ret == 1) {
				break;
			}
			int err = SSL_get_error(mSsl, ret);
			if (auto res = co_await handleError(stream, err); !res) {
			    co_return Err(res.error());
			}
		}
		co_return written;
	}

	auto shutdownImpl(StreamView stream) -> IoTask<void> {
		while (!mFail) { // In documentation it says, after SSL_ERROR_SSL, we should not call SSL_shutdown
		    int ret = SSL_shutdown(mSsl);
			if (ret == 1) { // Done in ssl layer, flush self and then call lowerlayer shutdown
				break;
			}
			int err = SSL_get_error(mSsl, ret);
			if (auto res = co_await handleError(stream, err); !res) {
			    co_return Err(res.error());
			}
		}
		if (auto res = co_await flushImpl(stream); !res) {
			co_return Err(res.error());
		}
		co_return co_await stream.shutdown();
	}

	// Flush the write buffer
	auto flushImpl(StreamView stream) -> IoTask<void> {
		auto data = mWriteBuffer.data();
		while (!data.empty()) {
			auto res = co_await stream.write(data);
			if (!res) {
				co_return Err(res.error());
			}
			if (*res == 0) {
				co_return Err(IoError::WriteZero);
			}
			mWriteBuffer.consume(*res);
			data = mWriteBuffer.data();
		}
		// Then flush the lower layer
		if (auto val = co_await stream.flush(); !val) {
			co_return Err(val.error());
		}
		mFlush = false;
		co_return {};
	}
};

auto registerBioMethod() -> void {
	bioMethod = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "ilias::TlsStream");
	BIO_meth_set_write_ex(bioMethod, [](BIO *b, const char *data, size_t len, size_t *ret) {
		return static_cast<TlsStateImpl*>(BIO_get_data(b))->bioWrite(data, len, ret);
	});
	BIO_meth_set_read_ex(bioMethod, [](BIO *b, char *data, size_t len, size_t *ret) {
		return static_cast<TlsStateImpl*>(BIO_get_data(b))->bioRead(data, len, ret);
	});
	BIO_meth_set_ctrl(bioMethod, [](BIO *b, int cmd, long num, void *ptr) {
		return static_cast<TlsStateImpl*>(BIO_get_data(b))->bioCtrl(cmd, num, ptr);
	});
}

auto unregisterBioMethod() -> void {
	BIO_meth_free(bioMethod);
	bioMethod = nullptr;
}

} // namespace openssl

#pragma region Export
using namespace openssl;

// Tls Context...
auto context::make() -> void * {
	struct Initializer {
		Initializer() {
			openssl::registerBioMethod();
		}
		~Initializer() {
			openssl::unregisterBioMethod();
		}
	};
	static Initializer init;

	auto ctxt = SSL_CTX_new(TLS_method());
	return ctxt;
}

auto context::destroy(void *ptr) -> void {
	SSL_CTX_free(static_cast<SSL_CTX *>(ptr));
}

// Tls State
auto TlsState::destroy() -> void {
	delete static_cast<TlsStateImpl *>(this);
}

auto TlsState::make(void *ctxt) -> TlsState * {
	auto impl = static_cast<SSL_CTX*>(ctxt);
	auto ptr = std::make_unique<TlsStateImpl>(impl);
	return ptr.release();
}

// Read
auto TlsState::read(StreamView stream, MutableBuffer buffer) -> IoTask<size_t> {
	return static_cast<TlsStateImpl *>(this)->readImpl(stream, buffer);
}

// Write
auto TlsState::write(StreamView stream, Buffer buffer) -> IoTask<size_t> {
	return static_cast<TlsStateImpl *>(this)->writeImpl(stream, buffer);
}

auto TlsState::flush(StreamView stream) -> IoTask<void> {
	return static_cast<TlsStateImpl *>(this)->flushImpl(stream);
}

auto TlsState::shutdown(StreamView stream) -> IoTask<void> {
	return static_cast<TlsStateImpl *>(this)->shutdownImpl(stream);
}

// Tls
auto TlsState::handshake(StreamView stream, TlsRole role) -> IoTask<void> {
	return static_cast<TlsStateImpl *>(this)->handshakeImpl(stream, role);
}

auto TlsState::setHostname(std::string_view hostname) -> void {
	SSL_set_tlsext_host_name(static_cast<TlsStateImpl *>(this)->mSsl, std::string(hostname).c_str());
}

auto TlsState::setAlpnProtocols(std::span<const std::string_view> protocols) -> bool {
	std::string buf;
	for (const auto &proto : protocols) {
		buf.push_back(char(proto.size()));
		buf.append(proto);
	}
	return SSL_set_alpn_protos(static_cast<TlsStateImpl *>(this)->mSsl, reinterpret_cast<const unsigned char*>(buf.data()), buf.size()) == 0;
}

auto TlsState::alpnSelected() const -> std::string_view {
	const uint8_t *ptr = nullptr;
	unsigned int len;
	SSL_get0_alpn_selected(static_cast<const TlsStateImpl *>(this)->mSsl, &ptr, &len);
	return std::string_view(reinterpret_cast<const char*>(ptr), len);
}

} // namespace tls

ILIAS_NS_END