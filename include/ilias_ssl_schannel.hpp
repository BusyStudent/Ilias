#pragma once

#include "ilias.hpp"
#include "ilias_backend.hpp"
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>

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
        mSsl(other.mSsl), mStreamSizes(other.mStreamSizes) 
    {
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
    }
    auto _handleshakeAsClient() -> Task<> {
        auto table = mCtxt->table();
        auto credHandle = mCtxt->credHandle();
        // Prepare memory
        std::unique_ptr<uint8_t []> imcomimg; //< The incoming buffer
        imcomimg = std::make_unique<uint8_t []>(1024 * 8);
        size_t received = 0;
        size_t imcomingSize = 1024 * 8;

        // Prepare
        ::CtxtHandle *ctxt = nullptr;
        for (;;) {
            ::SecBuffer inbuffers[2] { };
            inbuffers[0].BufferType = SECBUFFER_TOKEN;
            inbuffers[0].pvBuffer = imcomimg.get();
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
                SCHANNEL_LOG("SECBUFFER_EXTRA for %d\n", int(inbuffers[1].cbBuffer));
                ::memmove(imcomimg.get(), imcomimg.get() + (received - inbuffers[1].cbBuffer), inbuffers[1].cbBuffer);
                received = inbuffers[1].cbBuffer;
            }
            else {
                // All processed
                received = 0;
            }

            if (status == SEC_E_OK) {
                SCHANNEL_LOG("handshake done\n");
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
                SCHANNEL_LOG("Failed to handshake %d\n", int(status));
                co_return Unexpected(Error::SSLUnknown);
            }

            // Read new data here after this
            auto n = co_await mFd.recv(imcomimg.get() + received, imcomingSize - received);
            if (!n) {
                co_return Unexpected(n.error());
            }
            if (*n == 0) {
                co_return Unexpected(Error::ConnectionAborted);
            }
            received = *n;
        }
        // Done
        table->QueryContextAttributesW(ctxt, SECPKG_ATTR_STREAM_SIZES, &mStreamSizes);
        co_return {};
    }
    auto _send(const void *_buffer, size_t n) -> Task<size_t> {
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
        co_return 0;
    }
protected:
    SslContext *mCtxt = nullptr;
    T           mFd; //< The target we input and output
    std::wstring mHost; //< The host, used as a ext
    ::CtxtHandle mSsl { }; //< The current ssl handle from schannel
    ::SecPkgContext_StreamSizes mStreamSizes {};
};

template <StreamClient T = IStreamClient>
class SslClient : public SslSocket<T> {
public:
    using SslSocket<T>::SslSocket;

    auto connect(const IPEndpoint &endpoint) -> Task<> {
        if (auto ret = co_await this->mFd.connect(endpoint); !ret) {
            co_return Unexpected(ret.error());
        }
        co_return co_await this->_handleshakeAsClient();
    }
    auto send(const void *buffer, size_t n) -> Task<size_t> {
        return this->_send(buffer, n);
    }
    auto recv(void *buffer, size_t n) -> Task<size_t> {
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