#pragma once

#include "ilias.hpp"
#include "ilias_backend.hpp"
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>

#if defined(_MSC_VER)
    #pragma comment(lib, "secur32.lib")
#endif

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
    SslSocket(SslContext &ctxt, T &&value) : mCtxt(&Ctxt), mFd(std::move(T)) { }
    SslSocket(const SslSocket &) = delete;
    ~SslSocket();

    auto _handleshakeAsClient() -> Task<void> {
        // Prepare memory
        std::unique_ptr<uint8_t []> imcomimg; //< The incoming buffer
        imcomimg = std::make_unique<uint8_t []>(1024 * 8);
        size_t received = 0;
        size_t imcomingSize = 1024 * 8;

        for (;;) {
            // Prepare
            ::CtxtHandle *ctxt = nullptr;

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
            auto host = mHost.empty() ? nullptr : mHost.c_str();
            auto status = ::InitializeSecurityContextW(
                mCtxt->mCredHandle,
                ctxt,
                ctxt ? nullptr : host,
                flags,
                0,
                0,
                context ? &indesc : nullptr,
                0,
                &mSsl,
                &outdesc,
                &flags,
                nullptr
            );
            ctxt = &mSsl;

            // Check buffer here

            if (status == SEC_E_OK) {
                co_return {};
            }
            else if (status == SEC_I_CONTINUE_NEEDED) {
                // We need to send the output buffer to remote
                auto buffer = (uint8_t*) outbuffers[0].pvBuffer;
                auto size = outbuffers[0].cbBuffer;

                while (size != 0) {
                    auto n = co_await mFd.send(buffer, size);
                    if (!n) {
                        ::FreeContextBuffer(buffer);
                        co_return Unexpected(n.error());
                    }
                    if (*n == 0) {
                        ::FreeContextBuffer(buffer);
                        co_return Unexpected(Error::ConnectionReset);
                    }
                    size -= *n;
                }
                ::FreeContextBuffer(buffer);
                // Done
            }
            else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                co_return Unexpected(Error::SSLUnknown);
            }

            // Read new data here
            auto n = co_await mFd.recv(imcomimg.get(), imcomingSize);
            if (!n) {
                co_return Unexpected(n.error());
            }
            if (n == 0) {
                co_return Unexpected(Error::ConnectionReset);
            }
            received = *n;
        }
    }
protected:
    SslContext *mCtxt = nullptr;
    T           mFd; //< The target we input and output
    ::CtxtHandle mSsl { }; //< The current ssl handle from schannel
    std::wstring mHost; //< The host, used as a ext
};

template <StreamClient T = IStreamClient>
class SslClient : public SslSocket<T> {
public:

    auto setHostname(std::wstring_view hostname) -> void {
        this->mHost = hostname;
    }
    auto setHostname(std::string_view hostname) -> void {
        auto len = ::MultiByteToWideChar(CP_UTF8, 0, hostname.data(), hostname.size(), nullptr, 0);
        this->mHost.resize(len);
        len = ::MultiByteToWideChar(CP_UTF8, 0, hostname.data(), hostname.size(), this->mHost.data(), len);
        this->mHostname
    }
};

// TODO:
template <StreamClient T = IStreamListener>
class SslListener : public SslSocket<T> {
public:

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