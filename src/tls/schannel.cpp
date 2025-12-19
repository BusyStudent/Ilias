/**
 * @file schannel.cpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The schannel implementation of tls component
 * @version 0.1
 * @date 2024-08-27
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include <ilias/detail/win32defs.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/tls.hpp>

#define SECURITY_WIN32
#include <VersionHelpers.h>
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>
#include <vector>

ILIAS_NS_BEGIN

#pragma region Schannel
namespace tls {
namespace schannel {

struct TlsContextImpl {
    TlsContextImpl() {
        mDll = ::LoadLibraryW(L"secur32.dll");
        if (mDll == nullptr) {
            ILIAS_ERROR("Schannel", "Failed to load secur32.dll");
            ILIAS_THROW(std::system_error(SystemError::fromErrno()));
        }
        auto InitInterfaceW = (decltype(::InitSecurityInterfaceW) *) ::GetProcAddress(mDll, "InitSecurityInterfaceW");
        mTable = InitInterfaceW();

        // Get Credentials
        wchar_t unispNname [] = UNISP_NAME_W;
        SCHANNEL_CRED cred { };
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_AUTO_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO;
        auto status = mTable->AcquireCredentialsHandleW(nullptr, unispNname, SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr, nullptr, &mCredHandle, nullptr);
        if (status != SEC_E_OK) {
            ILIAS_ERROR("Schannel", "Failed to AcquireCredentialsHandleW : {}", status);
            ILIAS_THROW(std::system_error(SystemError::fromErrno()));
        }
    }
    ~TlsContextImpl() {
        if (mTable) {
            mTable->FreeCredentialsHandle(&mCredHandle);
        }
        if (mDll) {
            ::FreeLibrary(mDll);
        }
    }

    ::HMODULE mDll = nullptr;
    ::PSecurityFunctionTableW mTable = nullptr;
    ::CredHandle mCredHandle {}; //<
    bool mHasAlpn = ::IsWindows8OrGreater(); // ALPN is on the Windows 8.1 and later
};

// State Here

class TlsStateImpl final : public TlsState {
public:
    // Tls State
    ::PSecurityFunctionTableW mTable = nullptr;
    ::CredHandle mCredHandle {}; // The credentials handle, taken from the context
    ::CtxtHandle mTls {}; // The current tls handle from schannel
    ::SecPkgContext_StreamSizes mStreamSizes { };
    ::SecPkgContext_ApplicationProtocol mAlpnResult { }; // The ALPN selected result
    bool mIsHandshakeDone = false;
    bool mIsShutdown = false;
    bool mIsExpired = false;

    // Tls Configure
    std::vector<std::byte> mAlpn;
    std::wstring mHostname;

    // Buffer
    size_t mDecryptedCosume = 0; // The decrypted data consumed in the mReadBuffer
    Buffer mDecryptedBuffer; // The decrypted read buffer
    FixedStreamBuffer<16384 + 100> mReadBuffer;  //< The incoming buffer 2 ** 14 (MAX TLS SIZE) + header + trailer
    FixedStreamBuffer<16384 + 100> mWriteBuffer;

    TlsStateImpl(TlsContextImpl &ctxt) : mTable(ctxt.mTable), mCredHandle(ctxt.mCredHandle) {

    }    

    ~TlsStateImpl() {
        if (!mIsShutdown) {
            std::ignore = applyControl(SCHANNEL_SHUTDOWN);
        }
        mTable->DeleteSecurityContext(&mTls);
    }

    // Method
    auto handshakeAsClient(StreamView stream) -> IoTask<void> {
        ILIAS_DEBUG("Schannel", "Handshake begin for {}", win32::toUtf8(mHostname));
        // Prepare
        ::CtxtHandle *ctxt = nullptr;

        for (;;) {
            auto readBuffer = mReadBuffer.data();

            ::SecBuffer alpnBuffer { };
            alpnBuffer.BufferType = SECBUFFER_APPLICATION_PROTOCOLS;
            alpnBuffer.pvBuffer = mAlpn.data();
            alpnBuffer.cbBuffer = mAlpn.size();

            ::SecBuffer inbuffers[2] { };
            inbuffers[0].BufferType = SECBUFFER_TOKEN;
            inbuffers[0].pvBuffer = readBuffer.data();
            inbuffers[0].cbBuffer = readBuffer.size();
            inbuffers[1].BufferType = SECBUFFER_EMPTY;

            ::SecBuffer outbuffers[1] { };
            outbuffers[0].BufferType = SECBUFFER_TOKEN;

            ::SecBufferDesc indesc { SECBUFFER_VERSION, ARRAYSIZE(inbuffers), inbuffers };
            ::SecBufferDesc outdesc { SECBUFFER_VERSION, ARRAYSIZE(outbuffers), outbuffers };
            ::SecBufferDesc alpnDesc { SECBUFFER_VERSION, 1, &alpnBuffer };
            ::SecBufferDesc *firstDesc = mAlpn.empty() ? nullptr : &alpnDesc; //< If we have alpn, we need to pass it

            ::DWORD flags = ISC_REQ_USE_SUPPLIED_CREDS | ISC_REQ_ALLOCATE_MEMORY | 
                        ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM;
            auto host = mHostname.empty() ? nullptr : mHostname.data();
            auto status = mTable->InitializeSecurityContextW(
                &mCredHandle,
                ctxt,
                ctxt ? nullptr : host,
                flags,
                0,
                0,
                ctxt ? &indesc : firstDesc,
                0,
                ctxt ? nullptr : &mTls,
                &outdesc,
                &flags,
                nullptr
            );
            ctxt = &mTls;

            // Check buffer here
            if (inbuffers[1].BufferType == SECBUFFER_EXTRA) {
                ILIAS_TRACE("Schannel", "SECBUFFER_EXTRA for {}", inbuffers[1].cbBuffer);
                mReadBuffer.consume(mReadBuffer.size() - inbuffers[1].cbBuffer);
            }
            else {
                // All processed
                mReadBuffer.consume(mReadBuffer.size());
            }

            if (status == SEC_E_OK) {
                break;
            }
            else if (status == SEC_I_CONTINUE_NEEDED) {
                // We need to send the output buffer to remote
                auto freeBuffer = [this](uint8_t *mem) {
                    mTable->FreeContextBuffer(mem);
                };
                auto buffer = (uint8_t*) outbuffers[0].pvBuffer;
                auto size = outbuffers[0].cbBuffer;
                // Make a guard
                std::unique_ptr<uint8_t, decltype(freeBuffer)> guard(buffer, freeBuffer);

                if (auto res = co_await stream.writeAll(makeBuffer(buffer, size)); !res) {
                    ILIAS_WARN("Schannel", "Failed to send handshake {}", res.error().message());
                    co_return Err(res.error());
                }
                // Done
            }
            else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                ILIAS_WARN("Schannel", "Failed to handshake {}", status);
                co_return Err(SystemError(status));
            }

            // Try to receive more data
            auto data = mReadBuffer.prepare(mReadBuffer.capacity() - mReadBuffer.size());
            if (data.empty()) { // The buffer is full, but we still can't complete the handshake
                ILIAS_WARN("Schannel", "Failed to handshake by recv buffer full, peer send too much data");
                co_return Err(IoError::Tls);
            }

            // Read new data here after this
            auto n = co_await stream.read(data);
            if (!n) {
                ILIAS_WARN("Schannel", "Failed to handshake by recv failed {}", n.error().message());
                co_return Err(n.error());
            }
            if (*n == 0) {
                co_return Err(IoError::UnexpectedEOF);
            }
            mReadBuffer.commit(*n);
        }
        // Get alpn result
        if (!mAlpn.empty()) {
            if (auto err = mTable->QueryContextAttributesW(ctxt, SECPKG_ATTR_APPLICATION_PROTOCOL, &mAlpnResult); err != SEC_E_OK) {
                co_return Err(SystemError(err));
            }
        }
        if (auto err = mTable->QueryContextAttributesW(ctxt, SECPKG_ATTR_STREAM_SIZES, &mStreamSizes); err != SEC_E_OK) {
            ILIAS_WARN("Schannel", "Failed to get stream sizes {}", err);
            co_return Err(SystemError(err));
        }
        mIsHandshakeDone = true;
        ILIAS_DEBUG("Schannel", "Handshake done, streamSize {{ .header = {}, trailer = {}, maxMessage = {} }}", mStreamSizes.cbHeader, mStreamSizes.cbTrailer, mStreamSizes.cbMaximumMessage);
        co_return {};
    }

    auto handshakeImpl(StreamView stream, TlsRole role) -> IoTask<void> {
        if (role == TlsRole::Server) {
            // return handshakeAsServer(stream);
            ::abort(); // TODO: Not implemented
        }
        else {
            return handshakeAsClient(stream);
        }
    }

    auto applyControl(DWORD token) -> IoResult<void> {
        ::SecBuffer inbuffer { };
        inbuffer.BufferType = SECBUFFER_TOKEN;
        inbuffer.pvBuffer = &token;
        inbuffer.cbBuffer = sizeof(token);
        ::SecBufferDesc indesc { SECBUFFER_VERSION, 1, &inbuffer };
        if (auto status = mTable->ApplyControlToken(&mTls, &indesc); status != SEC_E_OK) {
            ILIAS_WARN("Schannel", "Failed to ApplyControlToken {}", status);
            return Err(SystemError(status));
        }
        return {};
    }

    // Read
    auto readImpl(StreamView stream, MutableBuffer buffer) -> IoTask<size_t> {
        if (!mIsHandshakeDone) {
            co_return Err(IoError::Tls);
        }
        if (buffer.empty()) {
            co_return 0;
        }
        SECURITY_STATUS status;
        while (true) {
            if (!mDecryptedBuffer.empty()) { // Oh, we can
                auto n = std::min(buffer.size(), mDecryptedBuffer.size());
                ::memcpy(buffer.data(), mDecryptedBuffer.data(), n);

                // Advance it
                mDecryptedBuffer = mDecryptedBuffer.subspan(n);
                buffer = buffer.subspan(n);

                if (mDecryptedBuffer.empty()) { // All used
                    mReadBuffer.consume(mDecryptedCosume);
                    mDecryptedBuffer = {};
                    mDecryptedCosume = 0;
                }
                co_return n;
            }
            if (!mReadBuffer.empty()) { // Oh, has data, try to decrypt it
                ::SecBuffer buffers[4] { };

                buffers[0].BufferType = SECBUFFER_DATA;
                buffers[0].pvBuffer = mReadBuffer.data().data();
                buffers[0].cbBuffer = mReadBuffer.size();
                buffers[1].BufferType = SECBUFFER_EMPTY;
                buffers[2].BufferType = SECBUFFER_EMPTY;
                buffers[3].BufferType = SECBUFFER_EMPTY; // SECBUFFER_EXTRA

                ::SecBufferDesc desc { SECBUFFER_VERSION, ARRAYSIZE(buffers), buffers };

                status = mTable->DecryptMessage(&mTls, &desc, 0, nullptr);
                if (status == SEC_E_OK) { // A new message is ready
                    ILIAS_ASSERT(buffers[0].BufferType == SECBUFFER_STREAM_HEADER);
                    ILIAS_ASSERT(buffers[1].BufferType == SECBUFFER_DATA);
                    ILIAS_ASSERT(buffers[2].BufferType == SECBUFFER_STREAM_TRAILER);

                    mDecryptedBuffer = { static_cast<std::byte*>(buffers[1].pvBuffer), buffers[1].cbBuffer };
                    mDecryptedCosume = mReadBuffer.size() - (buffers[3].BufferType == SECBUFFER_EXTRA ? buffers[3].cbBuffer : 0);
                    continue;
                }
                else if (status == SEC_I_CONTEXT_EXPIRED) {
                    // Peer closed
                    ILIAS_TRACE("Schannel", "Peer closed connection in tls layer");
                    mIsExpired = true;
                    co_return 0;
                }
                else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                    ILIAS_WARN("Schannel", "Failed to decrypt {}", status);
                    co_return Err(SystemError(status));
                }
                // We need more data
            }
            // Try to read some
            auto data = mReadBuffer.prepare(mReadBuffer.capacity() - mReadBuffer.size());
            auto n = co_await stream.read(data);
            if (!n) { // Failed to read
                co_return Err(n.error());
            }
            if (*n == 0) {
                co_return Err(IoError::UnexpectedEOF);
            }
            mReadBuffer.commit(*n);
        }
    }

    // Write
    auto writeImpl(StreamView stream, Buffer buffer) -> IoTask<size_t> {
        if (!mIsHandshakeDone) {
            co_return Err(IoError::Tls);
        }
        auto sended = size_t(0);
        while (buffer.size() > 0) {
            auto many = std::min<size_t>(buffer.size(), mStreamSizes.cbMaximumMessage);
            auto tmpbuf = mWriteBuffer.prepare(mStreamSizes.cbHeader + many + mStreamSizes.cbTrailer);
            ILIAS_ASSERT(!tmpbuf.empty());

            ::SecBuffer inbuffers[3] { };
            inbuffers[0].BufferType = SECBUFFER_STREAM_HEADER;
            inbuffers[0].pvBuffer = tmpbuf.data();
            inbuffers[0].cbBuffer = mStreamSizes.cbHeader;
            inbuffers[1].BufferType = SECBUFFER_DATA;
            inbuffers[1].pvBuffer = tmpbuf.data() + mStreamSizes.cbHeader;
            inbuffers[1].cbBuffer = many;
            inbuffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
            inbuffers[2].pvBuffer = tmpbuf.data() + mStreamSizes.cbHeader + many;
            inbuffers[2].cbBuffer = mStreamSizes.cbTrailer;

            // Copy to the data tmpbuffer
            // Because MSDN says EncryptMessage will encrypt the message in place
            ::memcpy(inbuffers[1].pvBuffer, buffer.data(), many);

            ::SecBufferDesc indesc { SECBUFFER_VERSION, ARRAYSIZE(inbuffers), inbuffers };
            auto status = mTable->EncryptMessage(&mTls, 0, &indesc, 0);
            if (status != SEC_E_OK) {
                co_return Err(SystemError(status));
            }

            // Send all encrypted message to
            // mWriteBuffer.commit(tmpbuf.size()); We don't use commit, because we just temporarily use it
            if (auto res = co_await stream.writeAll(tmpbuf); !res) {
                ILIAS_WARN("Schannel", "Failed to send encrypted message {}", res.error().message());
                co_return Err(res.error());
            }

            // Move our send buffer
            sended += many;
            buffer = buffer.subspan(many);
        }
        co_return sended;
    }

    // Currently no-op, TODO: implement
    auto flushImpl(StreamView stream) -> IoTask<void> {
        return stream.flush();
    }

    auto shutdownImpl(StreamView stream) -> IoTask<void> {
        return stream.shutdown();
    }
};

} // namespace schannel

#pragma region Export

using namespace schannel;

// Export to user
auto context::make(uint32_t flags) -> void * { // Does it safe for exception?
    return std::make_unique<TlsContextImpl>().release();
}

auto context::destroy(void *data) -> void {
    delete static_cast<TlsContextImpl *>(data);
}

// Tls
auto TlsState::handshake(StreamView stream, TlsRole role) -> IoTask<void> {
    return static_cast<TlsStateImpl *>(this)->handshakeImpl(stream, role);        
}

auto TlsState::setHostname(std::string_view name) -> void {
    static_cast<TlsStateImpl *>(this)->mHostname = win32::toWide(name);
}

auto TlsState::setAlpnProtocols(std::span<const std::string_view> protocols) -> bool {
    // From curl/schannel.c
    if (!::IsWindows8OrGreater()) {
        ILIAS_WARN("Schannel", "ALPN is not supported by the context");
        return false;
    }
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
    for (std::string_view str : protocols) {
        *cur = uint8_t(str.size());
        ++cur;
        ::memcpy(cur, str.data(), str.size());
        cur += str.size();
    }

    *len = uint16_t(cur - now);
    *extLength = uint32_t(cur - buffer - sizeof(uint32_t));

    // Copy to inside the buffer
    static_cast<TlsStateImpl *>(this)->mAlpn.assign(reinterpret_cast<const std::byte*>(buffer), reinterpret_cast<const std::byte*>(cur));
    return true;
}

auto TlsState::alpnSelected() const -> std::string_view {
    auto self = static_cast<const TlsStateImpl *>(this);
    if (self->mAlpnResult.ProtoNegoStatus != SecApplicationProtocolNegotiationStatus_Success) {
        return {};
    }
    return std::string_view(reinterpret_cast<const char*>(self->mAlpnResult.ProtocolId), self->mAlpnResult.ProtocolIdSize);
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

// Another
auto TlsState::destroy() -> void {
    delete static_cast<TlsStateImpl *>(this);
}

auto TlsState::make(void *ctxt) -> TlsState * {
    auto impl = static_cast<TlsContextImpl *>(ctxt);
    return new TlsStateImpl(*impl);
}

} // namespace tls


ILIAS_NS_END