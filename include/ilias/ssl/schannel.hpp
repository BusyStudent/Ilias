#pragma once

#include "../net/traits.hpp"
#define SECURITY_WIN32
#include <VersionHelpers.h>
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>
#include <vector>

#undef min
#undef max

#if 1 && !defined(NDEBUG)
    #define SCHANNEL_LOG(fmt, ...) ::fprintf(stderr, fmt, __VA_ARGS__)
#else
    #define SCHANNEL_LOG(fmt, ...)
#endif


ILIAS_NS_BEGIN

namespace Schannel {

/**
 * @brief Error category for explain Schannel Error c
 * 
 */
class SslCategory final : public ErrorCategory {
public:
    auto name() const -> std::string_view override { 
        return "schannel"; 
    }
    auto message(int64_t code) const -> std::string override {
        // SChannel using the win32 error code
        return Error::fromErrno(code).message();
    }
    auto equivalent(int64_t code, const Error &other) const -> bool override {
        if (other.category() == IliasCategory::instance()) {
            switch (other.value()) {
                case Error::SSL:
                case Error::SSLUnknown: return true;
            }
        }
        return ErrorCategory::equivalent(code, other);
    }
    static auto instance() -> SslCategory & { static SslCategory ret; return ret; }
    static auto makeError(DWORD code) -> Error { return Error(code, instance()); }
};

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
            SCHANNEL_LOG("[SChannel] Failed to AcquireCredentialsHandleW : %d\n", int(status));
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
    auto hasAlpn() const noexcept {
        return mHasAlpn;
    }
private:
    ::HMODULE mDll = ::LoadLibraryA("secur32.dll");
    ::PSecurityFunctionTableW mTable = nullptr;
    ::CredHandle mCredHandle { }; //<
    bool mHasAlpn = ::IsWindows8Point1OrGreater(); //< ALPN is on the Windows 8.1 and later
};

/**
 * @brief Helper class for wrapping ssl handle and receive buffer
 * 
 */
class SslData {
public:
    SslData(::PSecurityFunctionTableW t) : table(t) { }
    SslData(const SslData &) = delete;
    ~SslData() {
        table->DeleteSecurityContext(&ssl);
    }

    ::PSecurityFunctionTableW table = nullptr;
    ::CtxtHandle ssl { }; //< The current ssl handle from schannel
    ::SecPkgContext_StreamSizes streamSizes { };

    // Buffer heres
    uint8_t incoming[16384 + 500]; //< The incoming buffer 2 ** 14 (MAX TLS SIZE) + header + trailer
    size_t incomingUsed = 0; //< How many bytes used in the buffer
    size_t incomingReceived = 0; //< How may bytes received in here
    size_t incomingCapicity = sizeof(incoming); //< The maxsize of the mIncoming
    uint8_t *decrypted = nullptr;
    size_t decryptedAvailable = 0; //< How many bytes useable in buffer 
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
    SslSocket(SslSocket &&other) = default;
    ~SslSocket() {
        close();
    }

    auto close() -> void {
        if (!mData) {
            return;
        }
        SCHANNEL_LOG("[Schannel] Close for %ls\n", mHost.c_str());
        mFd = T();
        mCtxt = nullptr;
        mData.reset();
    }
protected:
    auto _handshakeAsClient() -> Task<> {
        if (mData) {
            co_return {};
        }
        auto table = mCtxt->table();
        auto credHandle = mCtxt->credHandle();
        // Create the ssl data to prepare memory
        auto data = std::make_unique<SslData>(table);
        auto &received = data->incomingReceived;
        auto &incoming = data->incoming;
        auto &incomingCapicity = data->incomingCapicity;

        // Prepare
        ::CtxtHandle &ssl = data->ssl;
        ::CtxtHandle *ctxt = nullptr;

        SCHANNEL_LOG("[Schannel] handshake begin for %ls\n", mHost.c_str());
        for (;;) {
            ::SecBuffer alpnBuffer { };
            alpnBuffer.BufferType = SECBUFFER_APPLICATION_PROTOCOLS;
            alpnBuffer.pvBuffer = mAlpn.data();
            alpnBuffer.cbBuffer = mAlpn.size();

            ::SecBuffer inbuffers[2] { };
            inbuffers[0].BufferType = SECBUFFER_TOKEN;
            inbuffers[0].pvBuffer = incoming;
            inbuffers[0].cbBuffer = received;
            inbuffers[1].BufferType = SECBUFFER_EMPTY;

            ::SecBuffer outbuffers[1] { };
            outbuffers[0].BufferType = SECBUFFER_TOKEN;

            ::SecBufferDesc indesc { SECBUFFER_VERSION, ARRAYSIZE(inbuffers), inbuffers };
            ::SecBufferDesc outdesc { SECBUFFER_VERSION, ARRAYSIZE(outbuffers), outbuffers };
            ::SecBufferDesc alpnDesc { SECBUFFER_VERSION, 1, &alpnBuffer };
            ::SecBufferDesc *firstDesc = mAlpn.empty() ? nullptr : &alpnDesc; //< If we have alpn, we need to pass it

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
                ctxt ? &indesc : firstDesc,
                0,
                &ssl,
                &outdesc,
                &flags,
                nullptr
            );
            ctxt = &ssl;

            // Check buffer here
            if (inbuffers[1].BufferType == SECBUFFER_EXTRA) {
                SCHANNEL_LOG("[Schannel] SECBUFFER_EXTRA for %d\n", int(inbuffers[1].cbBuffer));
                ::memmove(incoming, incoming + (received - inbuffers[1].cbBuffer), inbuffers[1].cbBuffer);
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
                SCHANNEL_LOG("[Schannel] Failed to handshake 0x%x\n", status);
                co_return Unexpected(SslCategory::makeError(status));
            }
            // Try send too many data, but we can
            if (received == incomingCapicity) {
                co_return Unexpected(Error::SSLUnknown);
            }

            // Read new data here after this
            auto n = co_await mFd.recv(incoming + received, incomingCapicity - received);
            if (!n) {
                SCHANNEL_LOG("[Schannel] Failed to handshake by recv failed %s\n", n.error().toString().c_str());
                co_return Unexpected(n.error());
            }
            if (*n == 0) {
                co_return Unexpected(Error::ConnectionAborted);
            }
            received += *n;
        }
        table->QueryContextAttributesW(ctxt, SECPKG_ATTR_STREAM_SIZES, &data->streamSizes);
        mData = std::move(data);
        co_return {};
    }
    auto _send(const void *_buffer, size_t n) -> Task<size_t> {
        if (!mData) [[unlikely]] {
            if (auto ret = co_await _handshakeAsClient(); !ret) {
                co_return Unexpected(ret.error());
            }
        }
        auto &streamSizes = mData->streamSizes;
        auto buffer = static_cast<const uint8_t*>(_buffer);
        auto table = mCtxt->table();
        auto sended = 0;
        while (n > 0) {
            auto many = std::min<size_t>(n, streamSizes.cbMaximumMessage);
            auto tmpbuf = std::make_unique<uint8_t[]>(many + streamSizes.cbHeader + streamSizes.cbTrailer);

            ::SecBuffer inbuffers[3] { };
            inbuffers[0].BufferType = SECBUFFER_STREAM_HEADER;
            inbuffers[0].pvBuffer = tmpbuf.get();
            inbuffers[0].cbBuffer = streamSizes.cbHeader;
            inbuffers[1].BufferType = SECBUFFER_DATA;
            inbuffers[1].pvBuffer = tmpbuf.get() + streamSizes.cbHeader;
            inbuffers[1].cbBuffer = many;
            inbuffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
            inbuffers[2].pvBuffer = tmpbuf.get() + streamSizes.cbHeader + many;
            inbuffers[2].cbBuffer = streamSizes.cbTrailer;

            // Copy to the data tmpbuffer
            // Because MSDN says EncryptMessage will encrypt the message in place
            ::memcpy(inbuffers[1].pvBuffer, buffer, many);
            
            ::SecBufferDesc indesc { SECBUFFER_VERSION, ARRAYSIZE(inbuffers), inbuffers };
            auto status = table->EncryptMessage(&mData->ssl, 0, &indesc, 0);
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
        if (!mData) [[unlikely]] {
            if (auto ret = co_await _handshakeAsClient(); !ret) {
                co_return Unexpected(ret.error());
            }
        }
        auto &incoming = mData->incoming;
        auto &incomingUsed = mData->incomingUsed;
        auto &incomingCapicity = mData->incomingCapicity;
        auto &incomingReceived = mData->incomingReceived;
        auto &decryptedAvailable = mData->decryptedAvailable;
        auto &streamSizes = mData->streamSizes;        
        auto &decrypted = mData->decrypted;
        auto table = mCtxt->table();
        while (true) {
            if (decryptedAvailable) {
                //< Calc how many bytes we can take from this buffer
                n = std::min(decryptedAvailable, n);
                ::memcpy(buffer, decrypted, n);

                // Advance it
                decryptedAvailable -= n;
                decrypted += n;

                if (decryptedAvailable == 0) {
                    // All used
                    ::memmove(incoming, incoming + incomingUsed, incomingReceived - incomingUsed);
                    incomingReceived -= incomingUsed;
                    incomingUsed = 0;
                    decrypted = nullptr;
                }
                co_return n;
            }
            // We need recv data from T
            if (incomingReceived) {
                ::SecBuffer buffers[4] { };

                buffers[0].BufferType = SECBUFFER_DATA;
                buffers[0].pvBuffer = incoming;
                buffers[0].cbBuffer = incomingReceived;
                buffers[1].BufferType = SECBUFFER_EMPTY;
                buffers[2].BufferType = SECBUFFER_EMPTY;
                buffers[3].BufferType = SECBUFFER_EMPTY;

                ::SecBufferDesc desc { SECBUFFER_VERSION, ARRAYSIZE(buffers), buffers };

                auto status = table->DecryptMessage(&mData->ssl, &desc, 0, nullptr);
                if (status == SEC_E_OK) {
                    ILIAS_ASSERT(buffers[0].BufferType == SECBUFFER_STREAM_HEADER);
                    ILIAS_ASSERT(buffers[1].BufferType == SECBUFFER_DATA);
                    ILIAS_ASSERT(buffers[2].BufferType == SECBUFFER_STREAM_TRAILER);

                    decrypted = (uint8_t*) buffers[1].pvBuffer;
                    decryptedAvailable = buffers[1].cbBuffer;
                    // All used or not
                    incomingUsed = incomingReceived - (buffers[3].BufferType == SECBUFFER_EXTRA ? buffers[3].cbBuffer : 0);
                    continue;
                }
                else if (status == SEC_I_CONTEXT_EXPIRED) {
                    //< Peer closed
                    co_return 0;
                }
                else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                    SCHANNEL_LOG("[Schannel] Failed to decrypt 0x%x\n", status);
                    co_return Unexpected(SslCategory::makeError(status));
                }
            }
            if (incomingReceived == incomingCapicity) {
                // Max Packet Size, but still cannot decrypt
                SCHANNEL_LOG("[Schannel] Failed to decrypt, but incoming buffer is full\n");
                co_return Unexpected(Error::SSLUnknown);
            }

            // Try read data
            auto num = co_await mFd.recv(incoming + incomingReceived, incomingCapicity - incomingReceived);
            if (!num) {
                co_return Unexpected(num.error());
            }
            if (*num == 0) { //< Connection Closed
                co_return 0;
            }
            incomingReceived += *num;
        }
    }
    auto _disconnect() -> Task<> {
        if (!mData) {
            co_return {};
        }
        SCHANNEL_LOG("[Schannel] disconnect\n");

        // Send say goodbye 
        DWORD type = SCHANNEL_SHUTDOWN;
        
        ::SecBuffer inbuffer { };
        inbuffer.BufferType = SECBUFFER_TOKEN;
        inbuffer.pvBuffer = &type;
        inbuffer.cbBuffer = sizeof(type);

        ::SecBuffer outbuffer { };
        outbuffer.BufferType = SECBUFFER_EMPTY;
        outbuffer.pvBuffer = nullptr;
        outbuffer.cbBuffer = 0;

        ::SecBufferDesc indesc { SECBUFFER_VERSION, 1, &inbuffer };
        ::SecBufferDesc outdesc { SECBUFFER_VERSION, 1, &outbuffer };

        auto table = mCtxt->table();
        auto credHandle = mCtxt->credHandle();
        table->ApplyControlToken(&mData->ssl, &indesc);

        DWORD flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT 
            | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM;
        auto status = table->InitializeSecurityContextW(
            &credHandle, 
            &mData->ssl, 
            nullptr, 
            flags, 
            0, 
            0, 
            &outdesc, 
            0, 
            &mData->ssl, 
            &outdesc, 
            &flags, 
            nullptr
        );
        if (status == SEC_E_OK) {
            // Send the data
            auto freeBuffer = [table](uint8_t *mem) {
                table->FreeContextBuffer(mem);
            };
            auto buffer = (uint8_t*) outbuffer.pvBuffer;
            auto size = outbuffer.cbBuffer;
            // Make a guard
            std::unique_ptr<uint8_t, decltype(freeBuffer)> guard(buffer, freeBuffer);

            while (size) {
                auto n = co_await mFd.send(buffer, size);
                if (!n || *n == 0) {
                    break;
                }
                buffer += *n;
                size -= *n;
            }
        }

        close(); //< Close the T below us
        co_return {};
    }
    auto operator =(SslSocket &&other) -> SslSocket & = default;
protected:
    SslContext *mCtxt = nullptr;
    T           mFd; //< The target we input and output
    std::wstring mHost; //< The host, used as a ext
    std::string mAlpnSelected; //< The ALPN selected
    std::vector<uint8_t> mAlpn; //< The ALPN, used as a ext
    std::unique_ptr<SslData> mData; //< The Ssl data
};

template <StreamClient T = IStreamClient>
class SslClient final : public SslSocket<T> {
public:
    using SslSocket<T>::SslSocket;

    auto handshake() -> Task<> {
        return this->_handshakeAsClient();
    }
    auto connect(const IPEndpoint &endpoint) -> Task<> {
        if (auto ret = co_await this->mFd.connect(endpoint); !ret) {
            co_return Unexpected(ret.error());
        }
        co_return co_await this->_handshakeAsClient();
    }
    auto shutdown() -> Task<> {
        return this->_disconnect();
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
#if 0
    template <typename U>
    auto setAlpn(U &&container) -> void {
        // From curl/schannel.c
        // if (!this->mCtxt->hasAlpn()) {
        //     SCHANNEL_LOG("[Schannel] ALPN is not supported by the context\n");
        //     return;
        // }
        uint8_t buffer[128];
        auto now = buffer;

        // First 4 bytes are the length of the buffer
        auto extLength = reinterpret_cast<uint32_t*>(now);
        now += sizeof(uint32_t);

        // Next 4 bytes are the type of the extension
        auto flags = reinterpret_cast<uint32_t*>(now);
        now += sizeof(uint32_t);
        *flags = SecApplicationProtocolNegotiationExt_ALPN;

        auto len = reinterpret_cast<uint16_t*>(now);
        now += sizeof(uint16_t);

        // Generate the extension here
        auto cur = now;
        for (std::string_view str : container) {
            *cur = uint8_t(str.size());
            ++cur;
            ::memcpy(cur, str.data(), str.size());
            cur += str.size();
        }

        *len = uint16_t(cur - now);
        *extLength = uint32_t(cur - buffer + sizeof(uint32_t));

        // Copy to inside the buffer
        this->mAlpn.assign(buffer, cur);
    }
    auto alpnSelected() const -> std::string_view {
        return this->mAlpnSelected;
    }
#endif
};
static_assert(StreamClient<SslClient<> >);

// TODO:
template <StreamListener T = IStreamListener>
class SslListener final : public SslSocket<T> {
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