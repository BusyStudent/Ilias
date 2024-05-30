#pragma once

#include "ilias.hpp"
#include "ilias_ring.hpp"
#include "ilias_backend.hpp"
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>
#include <vector>

#undef min
#undef max

#if 1
    #define SCHANNEL_LOG(fmt, ...) ::printf(fmt, __VA_ARGS__)
#else
    #define SCHANNEL_LOG(fmt, ...)
#endif


ILIAS_NS_BEGIN

namespace Schannel {

/**
 * @brief The schannel version of SslContext
 * 
 */
class SslContext {
public:
    SslContext() {
        // Load the security dll
        auto InitInterfaceW = (decltype(::InitSecurityInterfaceW) *) ::GetProcAddress(mDll, "InitSecurityInterfaceW");
        mTable = InitInterfaceW();

        // Get Credentials
        wchar_t unispNname [] = UNISP_NAME_W;
        SCHANNEL_CRED cred { };
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_AUTO_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO;
        auto status = mTable->AcquireCredentialsHandleW(nullptr, unispNname, SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr, nullptr, &mCredHandle, nullptr);
        if (status != SEC_E_OK) {
            // TODO : ...
        }
    }
    SslContext(const SslContext &) = delete;
    ~SslContext() {
        mTable->FreeCredentialsHandle(&mCredHandle);
        ::FreeLibrary(mDll);
    }

    auto table() const noexcept {
        return mTable;
    }
    auto credHandle() const noexcept {
        return mCredHandle;
    }
private:
    ::HMODULE mDll = ::LoadLibraryA("secur32.dll");
    ::PSecurityFunctionTableW mTable = nullptr;
    ::CredHandle mCredHandle { }; //<
};

/**
 * @brief The common part of the ssl socket
 * 
 * @tparam T 
 */
template <typename T>
class SslSocket {
public:
    SslSocket() = default;
    SslSocket(SslContext &ctxt, T &&value) : mCtxt(&ctxt), mFd(std::move(value)) { }
    SslSocket(const SslSocket &) = delete;
    SslSocket(SslSocket &&other) : mCtxt(other.mCtxt), 
        mFd(std::move(other.mFd)), mHost(std::move(other.mHost)), 
        mSsl(other.mSsl), mStreamSizes(other.mStreamSizes), mIncoming(std::move(other.mIncoming))
    {
        mHandshaked = other.mHandshaked;
        mIncomingUsed = other.mIncomingUsed;
        mIncomingReceived = other.mIncomingReceived;
        mDecrypted = other.mDecrypted;
        mDecryptedAvailable = other.mDecryptedAvailable;

        other.mIncomingUsed = 0;
        other.mIncomingReceived = 0;
        other.mDecryptedAvailable = 0;
        other.mDecrypted = nullptr;
        other.mHandshaked = false;
        other.mCtxt = nullptr;
        other.mSsl = { };
    }
    ~SslSocket() {
        close();
    }

    auto close() -> void {
        if (!mCtxt) {
            return;
        }
        mCtxt->table()->DeleteSecurityContext(&mSsl);
        mFd = T();
        mCtxt = nullptr;
        mSsl = { };
        mHost.clear();
        mStreamSizes = { };
        mIncoming.reset();
        mIncomingUsed = 0;
        mIncomingReceived = 0;
        mDecrypted = nullptr;
        mDecrypted = 0;
        mHandshaked = false;
    }
protected:
    auto _handshakeAsClient() -> Task<> {
        if (mHandshaked) {
            co_return {};
        }
        auto table = mCtxt->table();
        auto credHandle = mCtxt->credHandle();
        // Prepare memory
        auto &received = mIncomingReceived;
        mIncoming = std::make_unique<uint8_t []>(mIncomingCapicity); //< The incoming buffer
        received = 0;

        // Prepare
        ::CtxtHandle *ctxt = nullptr;
        for (;;) {
            ::SecBuffer inbuffers[2] { };
            inbuffers[0].BufferType = SECBUFFER_TOKEN;
            inbuffers[0].pvBuffer = mIncoming.get();
            inbuffers[0].cbBuffer = received;
            inbuffers[1].BufferType = SECBUFFER_EMPTY;

            ::SecBuffer outbuffers[1] { };
            outbuffers[0].BufferType = SECBUFFER_TOKEN;

            ::SecBufferDesc indesc { SECBUFFER_VERSION, ARRAYSIZE(inbuffers), inbuffers };
            ::SecBufferDesc outdesc { SECBUFFER_VERSION, ARRAYSIZE(outbuffers), outbuffers };

            DWORD flags = ISC_REQ_USE_SUPPLIED_CREDS | ISC_REQ_ALLOCATE_MEMORY | 
                        ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM;
            auto host = mHost.empty() ? nullptr : mHost.data();
            auto status = table->InitializeSecurityContextW(
                &credHandle,
                ctxt,
                host,
                flags,
                0,
                0,
                ctxt ? &indesc : nullptr,
                0,
                &mSsl,
                &outdesc,
                &flags,
                nullptr
            );
            ctxt = &mSsl;

            // Check buffer here
            if (inbuffers[1].BufferType == SECBUFFER_EXTRA) {
                SCHANNEL_LOG("[Schannel] SECBUFFER_EXTRA for %d\n", int(inbuffers[1].cbBuffer));
                ::memmove(mIncoming.get(), mIncoming.get() + (received - inbuffers[1].cbBuffer), inbuffers[1].cbBuffer);
                received = inbuffers[1].cbBuffer;
            }
            else {
                // All processed
                received = 0;
            }

            if (status == SEC_E_OK) {
                SCHANNEL_LOG("[Schannel] handshake done\n");
                break;
            }
            else if (status == SEC_I_CONTINUE_NEEDED) {
                // We need to send the output buffer to remote
                auto freeBuffer = [table](uint8_t *mem) {
                    table->FreeContextBuffer(mem);
                };
                auto buffer = (uint8_t*) outbuffers[0].pvBuffer;
                auto size = outbuffers[0].cbBuffer;
                // Make a guard
                std::unique_ptr<uint8_t, decltype(freeBuffer)> guard(buffer, freeBuffer);

                while (size != 0) {
                    auto n = co_await mFd.send(buffer, size);
                    if (!n) {
                        co_return Unexpected(n.error());
                    }
                    if (*n == 0) {
                        co_return Unexpected(Error::ConnectionAborted);
                    }
                    size -= *n;
                    buffer += *n;
                }
                // Done
            }
            else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                SCHANNEL_LOG("[Schannel] Failed to handshake %d\n", int(status));
                co_return Unexpected(Error::SSLUnknown);
            }
            // Try send too many data, but we can
            if (received == mIncomingCapicity) {
                co_return Unexpected(Error::SSLUnknown);
            }

            // Read new data here after this
            auto n = co_await mFd.recv(mIncoming.get() + received, mIncomingCapicity - received);
            if (!n) {
                co_return Unexpected(n.error());
            }
            if (*n == 0) {
                co_return Unexpected(Error::ConnectionAborted);
            }
            received = *n;
        }
        table->QueryContextAttributesW(ctxt, SECPKG_ATTR_STREAM_SIZES, &mStreamSizes);
        mHandshaked = true;
        co_return {};
    }
    auto _send(const void *_buffer, size_t n) -> Task<size_t> {
        if (!mHandshaked) {
            if (auto ret = co_await _handshakeAsClient(); !ret) {
                co_return Unexpected(ret.error());
            }
        }
        auto buffer = static_cast<const uint8_t*>(_buffer);
        auto table = mCtxt->table();
        auto sended = 0;
        while (n > 0) {
            auto many = std::min<size_t>(n, mStreamSizes.cbMaximumMessage);
            auto tmpbuf = std::make_unique<uint8_t[]>(many + mStreamSizes.cbHeader + mStreamSizes.cbTrailer);

            ::SecBuffer inbuffers[3] { };
            inbuffers[0].BufferType = SECBUFFER_STREAM_HEADER;
            inbuffers[0].pvBuffer = tmpbuf.get();
            inbuffers[0].cbBuffer = mStreamSizes.cbHeader;
            inbuffers[1].BufferType = SECBUFFER_DATA;
            inbuffers[1].pvBuffer = tmpbuf.get() + mStreamSizes.cbHeader;
            inbuffers[1].cbBuffer = many;
            inbuffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
            inbuffers[2].pvBuffer = tmpbuf.get() + mStreamSizes.cbHeader + many;
            inbuffers[2].cbBuffer = mStreamSizes.cbTrailer;

            // Copy to the data tmpbuffer
            // Because MSDN says EncryptMessage will encrypt the message in place
            ::memcpy(inbuffers[1].pvBuffer, buffer, many);
            
            ::SecBufferDesc indesc { SECBUFFER_VERSION, ARRAYSIZE(inbuffers), inbuffers };
            auto status = table->EncryptMessage(&mSsl, 0, &indesc, 0);
            if (status != SEC_E_OK) {
                co_return Unexpected(Error::SSLUnknown);
            }

            // Send all encrypted message to
            auto wbuffer = tmpbuf.get();
            auto total = inbuffers[0].cbBuffer + inbuffers[1].cbBuffer + inbuffers[2].cbBuffer;
            while (total > 0) {
                auto num = co_await mFd.send(wbuffer, total);
                if (!num) {
                    co_return Unexpected(num.error());
                }
                if (*num == 0) {
                    co_return Unexpected(Error::ConnectionAborted);
                }
                total -= *num;
                wbuffer += *num;
            }

            // Move our send buffer
            sended += many;
            n -= many;
            buffer += many;
        }
        co_return sended;
    }
    auto _recv(void *buffer, size_t n) -> Task<size_t> {
        if (!mHandshaked) {
            if (auto ret = co_await _handshakeAsClient(); !ret) {
                co_return Unexpected(ret.error());
            }
        }
        auto table = mCtxt->table();
        while (true) {
            if (mDecryptedAvailable) {
                //< Calc how many bytes we can take from this buffer
                n = std::min(mDecryptedAvailable, n);
                ::memcpy(buffer, mDecrypted, n);

                // Advance it
                mDecryptedAvailable -= n;
                mDecrypted += n;

                if (mDecryptedAvailable == 0) {
                    // All used
                    ::memmove(mIncoming.get(), mIncoming.get() + mIncomingUsed, mIncomingReceived - mIncomingUsed);
                    mIncomingReceived -= mIncomingUsed;
                    mIncomingUsed = 0;
                    mDecrypted = nullptr;
                }
                co_return n;
            }
            // We need recv data from T
            if (mIncomingReceived) {
                ::SecBuffer buffers[4] { };

                buffers[0].BufferType = SECBUFFER_DATA;
                buffers[0].pvBuffer = mIncoming.get();
                buffers[0].cbBuffer = mIncomingReceived;
                buffers[1].BufferType = SECBUFFER_EMPTY;
                buffers[2].BufferType = SECBUFFER_EMPTY;
                buffers[3].BufferType = SECBUFFER_EMPTY;

                ::SecBufferDesc desc { SECBUFFER_VERSION, ARRAYSIZE(buffers), buffers };

                auto status = table->DecryptMessage(&mSsl, &desc, 0, nullptr);
                if (status == SEC_E_OK) {
                    ILIAS_ASSERT(buffers[0].BufferType == SECBUFFER_STREAM_HEADER);
                    ILIAS_ASSERT(buffers[1].BufferType == SECBUFFER_DATA);
                    ILIAS_ASSERT(buffers[2].BufferType == SECBUFFER_STREAM_TRAILER);

                    mDecrypted = (uint8_t*) buffers[1].pvBuffer;
                    mDecryptedAvailable = buffers[1].cbBuffer;
                    // All used or not
                    mIncomingUsed = mIncomingReceived - (buffers[3].BufferType == SECBUFFER_EXTRA ? buffers[3].cbBuffer : 0);
                    continue;
                }
                else if (status == SEC_I_CONTEXT_EXPIRED) {
                    //< Peer closed
                    co_return 0;
                }
                else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                    SCHANNEL_LOG("[Schannel] Failed to decrypt %d\n", int(status));
                    co_return Unexpected(Error::SSLUnknown);
                }
            }

            // Try read data
            auto n = co_await mFd.recv(mIncoming.get() + mIncomingReceived, mIncomingCapicity - mIncomingReceived);
            if (!n) {
                co_return Unexpected(n.error());
            }
            if (*n == 0) { //< Connection Closed
                co_return 0;
            }
            mIncomingReceived += *n;
        }
    }
    auto _disconnect() -> Task<> {
        // Send say goodbye 
        close(); //< Close the T below us
        co_return {};
    }
    auto operator =(SslSocket &&other) -> SslSocket & {
        if (this == &other) {
            return *this;
        }
        close();

        mHandshaked = other.mHandshaked;
        mCtxt = other.mCtxt;
        mFd = std::move(other.mFd);
        mHost = std::move(other.mHost);
        mSsl = other.mSsl;
        mStreamSizes = other.mStreamSizes;
        mIncoming = std::move(other.mIncoming);
        mIncomingUsed = other.mIncomingUsed;
        mIncomingReceived = other.mIncomingReceived;
        mDecrypted = other.mDecrypted;
        mDecryptedAvailable = other.mDecryptedAvailable;

        other.mIncomingUsed = 0;
        other.mIncomingReceived = 0;
        other.mDecryptedAvailable = 0;
        other.mDecrypted = nullptr;
        other.mHandshaked = false;
        other.mCtxt = nullptr;
        other.mSsl = { };
        return *this;
    }
protected:
    bool mHandshaked = false;
    SslContext *mCtxt = nullptr;
    T           mFd; //< The target we input and output
    std::wstring mHost; //< The host, used as a ext
    ::CtxtHandle mSsl { }; //< The current ssl handle from schannel
    ::SecPkgContext_StreamSizes mStreamSizes { };
    std::unique_ptr<uint8_t []> mIncoming; //< The incoming buffer
    size_t mIncomingUsed = 0; //< How many bytes used in the buffer
    size_t mIncomingReceived = 0; //< How may bytes received in here
    size_t mIncomingCapicity = 16384; //< The maxsize of the mIncoming
    uint8_t *mDecrypted = nullptr;
    size_t mDecryptedAvailable = 0; //< How many bytes useable in buffer 
};

template <StreamClient T = IStreamClient>
class SslClient : public SslSocket<T> {
public:
    using SslSocket<T>::SslSocket;

    auto connect(const IPEndpoint &endpoint) -> Task<> {
        if (auto ret = co_await this->mFd.connect(endpoint); !ret) {
            co_return Unexpected(ret.error());
        }
        co_return co_await this->_handshakeAsClient();
    }
    auto send(const void *buffer, size_t n) -> Task<size_t> {
        return this->_send(buffer, n);
    }
    auto write(const void *buffer, size_t n) -> Task<size_t> {
        return this->_send(buffer, n);
    }
    auto recv(void *buffer, size_t n) -> Task<size_t> {
        return this->_recv(buffer, n);
    }
    auto read(void *buffer, size_t n) -> Task<size_t> {
        return this->_recv(buffer, n);
    }
    auto setHostname(std::wstring_view hostname) -> void {
        this->mHost = hostname;
    }
    auto setHostname(std::string_view hostname) -> void {
        auto len = ::MultiByteToWideChar(CP_UTF8, 0, hostname.data(), hostname.size(), nullptr, 0);
        this->mHost.resize(len);
        len = ::MultiByteToWideChar(CP_UTF8, 0, hostname.data(), hostname.size(), this->mHost.data(), len);
    }
};
static_assert(StreamClient<SslClient<> >);

// TODO:
template <StreamClient T = IStreamListener>
class SslListener : public SslSocket<T> {
public:
    SslListener() = delete; //< Not Impl
private:

};

}

// export it to namespace
#if defined(ILIAS_SSL_USE_SCHANNEL)
using Schannel::SslClient;
using Schannel::SslListener;
using Schannel::SslContext;
#endif

ILIAS_NS_END