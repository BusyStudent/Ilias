#pragma once

#include "ilias.hpp"
#include "ilias_backend.hpp"

#if defined(__cpp_impl_coroutine)
#include "ilias_co.hpp"
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <memory> //< for std::make_unique<T []>

ILIAS_NS_BEGIN

class SslContext {
public:
    SslContext() {
        SSL_library_init();

        mCtxt = SSL_CTX_new(SSLv23_method());
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
 * @brief Ring Buffer
 * 
 */
template <size_t N>
class SslRing {
public:
    SslRing() = default;
    SslRing(const SslRing &) = delete;
    ~SslRing() = default;

    bool empty() const noexcept {
        return mSize == 0;
    }
    bool full() const noexcept {
        return mSize == mCapicity;
    }
    void clear() noexcept {
        mSize = 0;
        mHead = 0;
        mTail = 0;
    }
    size_t size() const noexcept {
        return mSize;
    }
    size_t capacity() const noexcept { 
        return mCapicity; 
    }
    bool push(uint8_t value) noexcept {
        if (full()) {
            return 0;
        }
        mBuffer[mTail] = (value);
        mTail          = (mTail + 1) % mCapicity;
        ++mSize;
        return true;
    }
    bool pop(uint8_t &value) noexcept {
        if (empty()) {
            return 0;
        }
        value = (mBuffer[mHead]);
        mHead = (mHead + 1) % mCapicity;
        --mSize;
        return true;
    }
    size_t push(const void *buffer, size_t n) noexcept {
        int copySize = std::min(n, mCapicity - mSize);
        int cp1 = mTail + copySize > mCapicity ? mCapicity - mTail : copySize;
        ::memcpy(mBuffer + mTail, buffer, cp1);
        ::memcpy(mBuffer, (uint8_t *)buffer + cp1, copySize - cp1);

#ifdef ILIAS_SSL_RING_DEBUG
        for (int i = 0; i < mCapicity; ++i) {
            ::printf("%03d ", mBuffer[i]);
        }
        ::printf("\n");
        for (int i = 0; i < mCapicity; ++i) {
            if (cp1 == copySize) {
                ::printf("%s", (i >= mTail && i < mTail + copySize) ? "^^^ " : "    ");
            }
            else {
                ::printf("%s", (i < (mTail + copySize) % mCapicity || i >= mTail) ? "^^^ " : "    ");
            }
        }
        ::printf("\n");
#endif

        mTail = (copySize == cp1) ? mTail + copySize : mTail + copySize - mCapicity;
        mTail = (mTail == mCapicity) ? 0 : mTail;
        mSize += copySize;

#ifdef ILIAS_SSL_RING_DEBUG
        for (int i = 0; i < mCapicity; ++i) {
            if (i == mHead && i == mTail) {
                ::printf(">HT<");
            }
            else if (i == mHead) {
                ::printf(">H<<");
            }
            else if (i == mTail) {
                ::printf(">T<<");
            }
            else {
                ::printf("    ");
            }
        }
        ::printf("\n");
#endif

        return copySize;
    }
    size_t pop(void *buffer, size_t n) noexcept {
        int copySize = std::min(n, mSize);
        int cp1 = mHead + copySize > mCapicity ? mCapicity - mHead : copySize;
        ::memcpy(buffer, mBuffer + mHead, cp1);
        ::memcpy((uint8_t *)buffer + cp1, mBuffer, copySize - cp1);

#ifdef ILIAS_SSL_RING_DEBUG
        for (int i = 0; i < mCapicity; ++i) {
            ::printf("%03d ", mBuffer[i]);
        }
        ::printf("\n");
        for (int i = 0; i < mCapicity; ++i) {
            if (cp1 == copySize) {
                ::printf("%s", (i >= mHead && i < mHead + copySize) ? "### " : "    ");
            }
            else {
                ::printf("%s", (i < (mHead + copySize) % mCapicity || i >= mHead) ? "### " : "    ");
            }
        }
        ::printf("\n");
#endif

        if (copySize == mSize) {
            mHead = mTail = 0;
            mSize = 0;
        }
        else {
            mHead = cp1 == copySize ? mHead + copySize : mHead + copySize - mCapicity;
            mHead = mHead == mCapicity ? 0 : mHead;
            mSize -= copySize;
        }

#ifdef ILIAS_SSL_RING_DEBUG
        for (int i = 0; i < mCapicity; ++i) {
            if (i == mHead && i == mTail) {
                ::printf(">HT<");
            }
            else if (i == mHead) {
                ::printf(">>H<");
            }
            else if (i == mTail) {
                ::printf(">>T<");
            }
            else {
                ::printf("    ");
            }
        }
        ::printf("\n");
        ::printf("mSize :%zu\n", mSize);
#endif
        return copySize;
    }
private:
    size_t mCapicity = N;
    size_t mSize = 0;
    size_t mHead = 0;
    size_t mTail = 0;
    uint8_t mBuffer[N] = {0};
};

/**
 * @brief Wrapp T into BIO
 * 
 * @tparam T 
 */
template <typename T>
class SslBio {
public:
    SslBio(T &&f) : mFd(std::move(f)) {
        static Method method = _register();
        mBio = BIO_new(method.method);
        BIO_set_data(mBio, this);
        BIO_set_init(mBio, 1);
        BIO_set_shutdown(mBio, 0);
    }
    SslBio(const SslBio &) = delete;
    ~SslBio() {

    }
private:
    struct Method {
        Method(BIO_METHOD *method) : method(method) { }
        Method(const Method &) = delete;
        ~Method() {
            BIO_meth_free(method);
        }
        BIO_METHOD *method;
    };
    static BIO_METHOD *_register() {
        BIO_METHOD *method = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "IliasSslBio");
        BIO_meth_set_write(method, [](BIO *b, const char *data, int len) {
            return static_cast<SslBio*>(BIO_get_data(b))->_write(data, len);
        });
        BIO_meth_set_read(method, [](BIO *b, char *data, int len) {
            return static_cast<SslBio*>(BIO_get_data(b))->_read(data, len);
        });
        BIO_meth_set_ctrl(method, [](BIO *b, int cmd, long num, void *ptr) {
            return static_cast<SslBio*>(BIO_get_data(b))->_ctrl(cmd, num, ptr);
        });
        return method;
    }
    int _write(const char *data, int len) {
        if (!data) {
            return 0;
        }
        BIO_clear_retry_flags(mBio);
        if (mWriteRing.full()) {
            BIO_set_retry_write(mBio);
            return 0;
        }
        return mWriteRing.push(data, len);
    }
    int _read(char *data, int len) {
        if (!data) {
            return 0;
        }
        BIO_clear_retry_flags(mBio);
        if (mReadRing.empty()) {
            BIO_set_retry_read(mBio);
            return 0;
        }
        return mReadRing.pop(data, len);
    }
    long _ctrl(int cmd, long num, void *ptr) {
        switch (cmd) {
            case BIO_CTRL_FLUSH: mFlush = true; return 1;
        }
        return 0; 
    }

    T    mFd;
    BIO *mBio = nullptr;
    SslRing<1024 * 8> mReadRing;
    SslRing<1024 * 8> mWriteRing;
    bool              mFlush = false;
template <typename U>
friend class SslClient;
};

template <typename T>
class SslClient {
public:
    SslClient() = default;
    SslClient(SslContext &ctxt, T &&f) {
        mBio = new SslBio<T>(std::move(f));
        mSsl = SSL_new(ctxt.get());
        SSL_set_connect_state(mSsl);
        SSL_set_bio(mSsl, mBio->mBio, mBio->mBio);
        SSL_set_mode(mSsl, SSL_MODE_AUTO_RETRY);
    }
    ~SslClient() {
        SSL_free(mSsl);
        delete mBio;
    }

#if defined(__cpp_impl_coroutine)
    auto connect(const IPEndpoint &endpoint, int64_t timeout = -1) -> Task<ConnectHandlerArgs> {
        auto ret = co_await mBio->mFd.connect(endpoint, timeout);
        if (!ret) {
            co_return ret;
        }
        while (true) {
            int sslCon = SSL_connect(mSsl);
            if (sslCon == 1) {
                co_return ConnectHandlerArgs();
            }
            int errcode = SSL_get_error(mSsl, sslCon);
            if (auto ret = co_await _handleError(errcode, timeout); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
    auto recv(void *buffer, size_t n, int64_t timeout = -1) -> Task<RecvHandlerArgs> {
        while (true) {
            size_t readed = 0;
            int readret = SSL_read_ex(mSsl, buffer, n, &readed);
            if (readret == 1) {
                co_return readed;
            }
            int errcode = 0;
            errcode = SSL_get_error(mSsl, readret);
            if (auto ret = co_await _handleError(errcode, timeout); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
    auto send(const void *buffer, size_t n, int64_t timeout = -1) -> Task<RecvHandlerArgs> {
        while (true) {
            size_t written = 0;
            int writret = SSL_write_ex(mSsl, buffer, n, &written);
            if (writret == 1) {
                co_return written;
            }
            int errcode = 0;
            errcode = SSL_get_error(mSsl, writret);
            if (auto ret = co_await _handleError(errcode, timeout); !ret) {
                co_return Unexpected(ret.error());
            }
        }
    }
    auto _handleError(int errcode, int64_t timeout) -> Task<Expected<void, SockError> > {
        if (errcode == SSL_ERROR_WANT_READ) {
            auto ret = co_await _waitReadable(timeout);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
        }
        else if (errcode == SSL_ERROR_WANT_WRITE) {
            auto ret = co_await _flushWrite(timeout);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
        }
        else {
            // Error
            co_return Unexpected(SockError());
        }
        co_return Expected<void, SockError>();
    }
    auto _flushWrite(int64_t timeout) -> Task<Expected<void, SockError> > {
        // Flush the data
        size_t dataLeft = mBio->mWriteRing.size();
        auto tmpbuffer = std::make_unique<uint8_t[]>(dataLeft);
        mBio->mWriteRing.pop(tmpbuffer.get(), dataLeft);

        auto current = tmpbuffer.get();
        while (dataLeft > 0) {
            // FIXME: if timeout, it will lost data, we should add peek and discard method in ringbuffer
            auto ret = co_await mBio->mFd.send(current, dataLeft, timeout);
            if (!ret) {
                co_return Unexpected(ret.error()); //< Send Error
            }
            dataLeft -= *ret;
            current += *ret;
        }
        co_return Expected<void, SockError>();
    }
    auto _waitReadable(int64_t timeout) -> Task<Expected<void, SockError> > {
        if (!mBio->mWriteRing.empty()) {
            // Require flush
            auto ret = co_await _flushWrite(timeout);
            if (!ret) {
                co_return ret;
            }
        }

        size_t ringLeft = mBio->mReadRing.capacity() - mBio->mReadRing.size();
        auto tmpbuffer = std::make_unique<uint8_t[]>(ringLeft);
        auto ret = co_await mBio->mFd.recv(tmpbuffer.get(), ringLeft, timeout);
        if (!ret) {
            co_return Unexpected(ret.error()); //< Read Error
        }
        auto written = mBio->mReadRing.push(tmpbuffer.get(), *ret);
        ILIAS_ASSERT(written == *ret);
        co_return Expected<void, SockError>();
    }
#endif

private:
    SSL *mSsl = nullptr;
    SslBio<T> *mBio = nullptr;
};

ILIAS_NS_END