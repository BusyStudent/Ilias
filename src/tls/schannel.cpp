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
#include <ilias/detail/scope_exit.hpp>
#include <ilias/detail/win32defs.hpp>
#include <ilias/io/system_error.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/tls.hpp>
#include "../win32/ntdll.hpp"

#define SCHANNEL_USE_BLACKLISTS // For SCH_CREDENTIALS
#define SECURITY_WIN32
#include <VersionHelpers.h>
#include <winternl.h>
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>
#include <ncrypt.h>
#include <ranges>
#include <vector>

ILIAS_NS_BEGIN

// MARK: Schannel
namespace tls {
namespace schannel {
namespace {

// Context Here
#define WIN32_IMPORT(fn, dll) decltype(::fn) *fn = reinterpret_cast<decltype(::fn) *>(::GetProcAddress(dll, #fn))
#define SECUR_IMPORT(fn) WIN32_IMPORT(fn, mSecurDll)
#define CRYPT_IMPORT(fn) WIN32_IMPORT(fn, mCryptDll)
#define NCRYPT_IMPORT(fn) WIN32_IMPORT(fn, mNCryptDll)
#define ADVAPI_IMPORT(fn) WIN32_IMPORT(fn, mAdvapiDll)

struct TlsContextImpl {
    TlsContextImpl(uint32_t flags) {
        SecInvalidateHandle(&mClientCred);
        SecInvalidateHandle(&mServerCred);
        if (!InitSecurityInterfaceW) {
            ILIAS_ERROR("Schannel", "Failed to load crypto library secur32.dll");
            ILIAS_THROW(std::system_error(SystemError::fromErrno()));
        }
        mTable = InitSecurityInterfaceW();

        // Initialize the root store
        mRootStore = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0, 0, nullptr);
        mRootMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
        if (!mRootStore || !mRootMemStore) {
            ILIAS_ERROR("Schannel", "Failed to create root store");
            ILIAS_THROW(std::system_error(SystemError::fromErrno()));
        }
        if (!CertAddStoreToCollection(mRootStore, mRootMemStore, 0, 0)) {
            ILIAS_ERROR("Schannel", "Failed to add root store to collection");
            ILIAS_THROW(std::system_error(SystemError::fromErrno()));
        }
        // Check if we need to add the system roots
        if (!(flags & TlsContext::NoDefaultRootCerts)) {
            loadDefaultRootCerts();
        }
        if (!(flags & TlsContext::NoVerify)) {
            setVerify(true);
        }
    }

    ~TlsContextImpl() {
        if (mTable) {
            if (SecIsValidHandle(&mClientCred)) {
                mTable->FreeCredentialsHandle(&mClientCred);
            }
            if (SecIsValidHandle(&mServerCred)) {
                mTable->FreeCredentialsHandle(&mServerCred);
            }
        }
        if (mRootMemStore) {
            CertCloseStore(mRootMemStore, 0);
        }
        if (mRootStore) {
            CertCloseStore(mRootStore, 0);
        }
        if (mCertContext) {
            CertFreeCertificateContext(mCertContext);
        }
        if (mCertKey) {
            CryptDestroyKey(mCertKey);
        }
        if (mCertKeyProvider) {
            CryptReleaseContext(mCertKeyProvider, 0);
        }
        if (!mCertContainer.empty()) {
            // Remove the container, avoid leak the fucking shit !!!!!!!
            ::HCRYPTPROV fuckShitAPI {};
            auto ok = CryptAcquireContextW(&fuckShitAPI, mCertContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
            if (!ok) {
                ILIAS_WARN("Schannel", "Failed to delete key container in destructor, err {}", SystemError::fromErrno());
            }
        }
        if (mSecurDll) {
            ::FreeLibrary(mSecurDll);
        }
        if (mCryptDll) {
            ::FreeLibrary(mCryptDll);
        }
        // if (mNCryptDll) {
        //     ::FreeLibrary(mNCryptDll);
        // }
        if (mAdvapiDll) {
            ::FreeLibrary(mAdvapiDll);
        }
    }

    // MARK: Public API
    auto setVerify(bool verify) -> void {
        mVerifyPeer = verify;
    }

    auto loadDefaultRootCerts() -> bool {
        if (mDefaultRootCertsLoaded) {
            return true;
        }
        auto root = CertOpenSystemStoreW(0, L"ROOT");
        if (!root) {
            return false;
        }
        auto ok = CertAddStoreToCollection(mRootStore, root, 0, 0);
        CertCloseStore(root, 0);
        mDefaultRootCertsLoaded = ok;
        return ok;
    }

    auto loadRootCerts(Buffer buffer) -> bool {
        auto certsString = std::string_view { reinterpret_cast<const char *>(buffer.data()), buffer.size() };
        auto delim = std::string_view {"-----END CERTIFICATE-----"};
        auto added = false;
        for (;;) {
            auto pos = certsString.find(delim);
            if (pos == std::string_view::npos) {
                break;
            }
            // Advance
            auto sub = certsString.substr(0, pos + delim.size());
            certsString.remove_prefix(pos + delim.size());

            auto der = pemToBinary(makeBuffer(sub));
            if (der.empty()) {
                break;
            }
            added = CertAddEncodedCertificateToStore(mRootMemStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, reinterpret_cast<const BYTE *>(der.data()), der.size(), CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
        }
        return added;
    }

    auto useCert(Buffer buffer) -> bool {
        if (auto prev = std::exchange(mCertContext, nullptr); prev) {
            CertFreeCertificateContext(prev);
        }
        auto der = pemToBinary(buffer);
        if (der.empty()) {
            return false;
        }
        mCertContext = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, reinterpret_cast<const BYTE *>(der.data()), der.size());
        if (!mCertContext) {
            return false;
        }
        return true;
    }

    // MARK: Private Key
    auto usePrivateKey(Buffer buffer, std::string_view password) -> bool {
        if (!mCertContext || !password.empty()) { // Not cert loaded, or the use the password (We currently unsupport it) 
            return false;
        }
        auto der = pemToBinary(buffer);
        if (der.empty()) {
            return false;
        }

        // Parse the RSA private key
        auto keyBlob = std::vector<std::byte> {};
        auto ecc = false;
        if (auto keyInfo = decodeObject(der, PKCS_PRIVATE_KEY_INFO); !keyInfo.empty()) { // PKCS#8
            auto key = reinterpret_cast<const ::CRYPT_PRIVATE_KEY_INFO *>(keyInfo.data());
            if (key->Algorithm.pszObjId == std::string_view {szOID_RSA_RSA}) {
                // PKCS#1 Got the key
                auto span = std::span { reinterpret_cast<const std::byte *>(key->PrivateKey.pbData), key->PrivateKey.cbData };
                keyBlob = decodeObject(span, PKCS_RSA_PRIVATE_KEY);
            }
            else if (key->Algorithm.pszObjId == std::string_view(szOID_ECC_PUBLIC_KEY)) {
                // TODO: Support ECC
                ecc = true;
                return false;
            }
            else {
                // Unsupported
                return false;
            }
        }
        else {
            return false;
        }

        // Check parsed?
        if (keyBlob.empty()) {
            return false;
        }

        // Import RSA
        ::HCRYPTPROV provHandle {};
        ::HCRYPTKEY keyHandle {};
        std::wstring provContainer {};
        auto scope = ScopeExit([&]() {
            if (keyHandle) {
                CryptDestroyKey(keyHandle);
            }
            if (provHandle) {
                CryptReleaseContext(provHandle, 0);
            }
            if (!provContainer.empty()) {
                ::HCRYPTPROV fuckShitAPI {};
                auto ok = CryptAcquireContextW(&fuckShitAPI, provContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
                if (!ok) {
                    ILIAS_WARN("Schannel", "Failed to delete key container");
                }
            }
        });
        provContainer += L"IliasTlsKeyContainer-";
        provContainer += std::to_wstring(::GetCurrentProcessId());
        provContainer += std::to_wstring(reinterpret_cast<uintptr_t>(this));
        provContainer += std::to_wstring(::GetTickCount());
        if (!CryptAcquireContextW(&provHandle, provContainer.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_SILENT | CRYPT_NEWKEYSET)) {
            return false;
        }
        if (!CryptImportKey(provHandle, reinterpret_cast<const BYTE *>(keyBlob.data()), keyBlob.size(), 0, 0, &keyHandle)) {
            return false;
        }

        // Link it
        ::CRYPT_KEY_PROV_INFO keyProvInfo {};
        keyProvInfo.pwszContainerName = const_cast<wchar_t *>(provContainer.c_str());
        keyProvInfo.pwszProvName = const_cast<wchar_t *>(MS_ENH_RSA_AES_PROV_W);
        keyProvInfo.dwProvType = PROV_RSA_AES;
        keyProvInfo.dwFlags = CRYPT_SILENT;
        keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;
        if (!CertSetCertificateContextProperty(mCertContext, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo)) {
            ILIAS_ERROR("Schannel", "CertSetCertificateContextProperty failed");
            return false;
        }

        // Done
        mCertKeyProvider = std::exchange(provHandle, {});
        mCertKey = std::exchange(keyHandle, {});
        mCertContainer.swap(provContainer);
        return true;
    }

    auto diagnoseKeyImport() -> void {
        if (!mCertContext) {
            ILIAS_ERROR("Schannel", "No certificate loaded");
            return;
        }

        auto pubKeyInfo = &mCertContext->pCertInfo->SubjectPublicKeyInfo;
        ILIAS_TRACE("Schannel", "Certificate public key algorithm: {}",  pubKeyInfo->Algorithm.pszObjId);
        ILIAS_TRACE("Schannel", "Certificate public key size: {} bits", pubKeyInfo->PublicKey.cbData * 8);

        // Check private key
        ::HCRYPTPROV_OR_NCRYPT_KEY_HANDLE keyHandle{};
        ::DWORD keySpec{};
        ::BOOL callerFree{};        
        if (CryptAcquireCertificatePrivateKey(
            mCertContext, 
            CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG, 
            nullptr,
            &keyHandle, 
            &keySpec, 
            &callerFree
        )) {
            ILIAS_TRACE("Schannel", "Private key found! keySpec={}", keySpec);
            if (callerFree) {
                if (keySpec == CERT_NCRYPT_KEY_SPEC) {
                    // NCrypt key
                }
                else {
                    CryptReleaseContext(keyHandle, 0);
                }
            }
        }
        else {
            ILIAS_ERROR("Schannel", "No private key associated: {:#x}", ::GetLastError());
        }
    }

    // MARK: Convert Utils
    auto pemToBinary(Buffer buffer) -> std::vector<std::byte> {
        ::DWORD resultLen = 0;
        auto str = reinterpret_cast<const char *>(buffer.data());
        auto result = std::vector<std::byte> {};
        if (!CryptStringToBinaryA(str, buffer.size(), CRYPT_STRING_ANY, nullptr, &resultLen, nullptr, nullptr)) {
            return result;
        }
        result.resize(resultLen);
        if (!CryptStringToBinaryA(str, buffer.size(), CRYPT_STRING_ANY, reinterpret_cast<BYTE *>(result.data()), &resultLen, nullptr, nullptr)) {
            result.clear();
        }
        return result;
    }
    
    auto decodeObject(Buffer buffer, LPCSTR keyType) -> std::vector<std::byte> {
        ::DWORD blobSize = 0;
        auto blob = std::vector<std::byte> {};
        if (!CryptDecodeObjectEx(PKCS_7_ASN_ENCODING | X509_ASN_ENCODING, keyType, reinterpret_cast<const BYTE *>(buffer.data()), buffer.size(), 0, nullptr, nullptr, &blobSize)) {
            return blob;
        }
        blob.resize(blobSize);
        if (!CryptDecodeObjectEx(PKCS_7_ASN_ENCODING | X509_ASN_ENCODING, keyType, reinterpret_cast<const BYTE *>(buffer.data()), buffer.size(), 0, nullptr, blob.data(), &blobSize)) {
            blob.clear();
        }
        return blob;
    }

    // MARK: Lazy init the cred handle
    auto credHandle(TlsRole role) -> ::CredHandle {
        // Get Credentials
        wchar_t unispNname [] = UNISP_NAME_W;
        ::DWORD flags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_AUTO_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO;
        ::DWORD bound = role == TlsRole::Client ? SECPKG_CRED_OUTBOUND : SECPKG_CRED_INBOUND;
        auto handle = role == TlsRole::Client ? &mClientCred : &mServerCred;

        // The user certs we use
        ::PCCERT_CONTEXT certs[1] = { nullptr };
        ::DWORD nCerts = 0;
        if (mCertContext) {
            certs[0] = mCertContext;
            nCerts = 1;
        }

        // Do lazy, if already init, no need to do anything
        if (SecIsValidHandle(handle)) {
            return *handle;   
        }

        // Try new api (Windows 10)
#if defined(SCH_CREDENTIALS_VERSION)
        if (win32::ntdll().isWindows10OrGreater()) {
            ::SCH_CREDENTIALS schCredentials {
                .dwVersion = SCH_CREDENTIALS_VERSION,
                .cCreds = nCerts, // useCert
                .paCred = certs,
                .hRootStore = mRootStore,
                .dwFlags = flags,
            };
            auto status = mTable->AcquireCredentialsHandleW(nullptr, unispNname, bound, nullptr, &schCredentials, nullptr, nullptr, handle, nullptr); 
            if (status == SEC_E_OK) {
                return *handle;
            }
        }
#endif // SCH_CREDENTIALS_VERSION

        // Legacy
        ::SCHANNEL_CRED schCred {
            .dwVersion = SCHANNEL_CRED_VERSION,
            .cCreds = nCerts, // useCert
            .paCred = certs,
            .hRootStore = mRootStore,
            .dwFlags = flags,
        };
        auto status = mTable->AcquireCredentialsHandleW(nullptr, unispNname, bound, nullptr, &schCred, nullptr, nullptr, handle, nullptr);
        if (status != SEC_E_OK) {
            if (nCerts != 0) {
                diagnoseKeyImport();
            }
            ILIAS_ERROR("Schannel", "Failed to AcquireCredentialsHandleW : {}", status);
            ILIAS_THROW(std::system_error(SystemError::fromErrno()));
        }
        return *handle;
    }

    // MARK: Context Member
    // Basic State
    ::HMODULE mSecurDll = ::LoadLibraryW(L"secur32.dll");
    ::HMODULE mCryptDll = ::LoadLibraryW(L"crypt32.dll");
    // ::HMODULE mNCryptDll = ::LoadLibraryW(L"ncrypt.dll");
    ::HMODULE mAdvapiDll = ::LoadLibraryW(L"advapi32.dll");
    ::PSecurityFunctionTableW mTable = nullptr;
    bool mHasAlpn = win32::ntdll().IsWindows8Point1OrGreater(); // ALPN is on the Windows 8.1 and later

    // Configure
    bool mDefaultRootCertsLoaded = false;
    bool mVerifyPeer = false;

    // Credentials
    ::CredHandle mClientCred {}; // Use for client SECPKG_CRED_OUTBOUND
    ::CredHandle mServerCred {}; // Use for server SECPKG_CRED_INBOUND
    ::HCERTSTORE mRootStore = nullptr;    // Collection for root ([optional] system + custom)
    ::HCERTSTORE mRootMemStore = nullptr; // The custom root store

    ::PCCERT_CONTEXT mCertContext = nullptr; // The certificate context (for useCert)
    ::HCRYPTPROV     mCertKeyProvider {}; // The key context (for usePrivateKey)
    ::HCRYPTKEY      mCertKey {}; // The key context (for usePrivateKey)
    std::wstring     mCertContainer {};

    // Secur32
    SECUR_IMPORT(InitSecurityInterfaceW);
    
    // Crypt32
    CRYPT_IMPORT(PFXImportCertStore);
    CRYPT_IMPORT(CertFindCertificateInStore);
    CRYPT_IMPORT(CertOpenSystemStoreW);
    CRYPT_IMPORT(CertAddStoreToCollection);
    CRYPT_IMPORT(CertFreeCertificateContext);
    CRYPT_IMPORT(CertCreateCertificateContext);
    CRYPT_IMPORT(CertAddEncodedCertificateToStore);
    CRYPT_IMPORT(CertSetCertificateContextProperty);
    CRYPT_IMPORT(CertOpenStore);
    CRYPT_IMPORT(CertCloseStore);
    CRYPT_IMPORT(CryptAcquireCertificatePrivateKey);
    CRYPT_IMPORT(CryptStringToBinaryA);
    CRYPT_IMPORT(CryptDecodeObjectEx);
    CRYPT_IMPORT(CryptQueryObject);

    // NCrypt for ECC
    // NCRYPT_IMPORT(NCryptImportKey);
    // NCRYPT_IMPORT(NCryptFreeObject);
    // NCRYPT_IMPORT(NCryptOpenStorageProvider);

    // Advapi for RSA
    ADVAPI_IMPORT(CryptAcquireContextW);
    ADVAPI_IMPORT(CryptReleaseContext);
    ADVAPI_IMPORT(CryptImportKey);
    ADVAPI_IMPORT(CryptDestroyKey);
};

#undef SECUR_IMPORT
#undef CRYPT_IMPORT
#undef WIN32_IMPORT

// State Here
class TlsStateImpl final : public TlsState {
public:
    // Tls State
    TlsContextImpl &mCtxt;
    ::PSecurityFunctionTableW mTable = nullptr;
    ::CredHandle mCredHandle {}; // The credentials handle, taken from the context
    ::CtxtHandle mTls {}; // The current tls handle from schannel
    ::SecPkgContext_StreamSizes mStreamSizes {};
    ::SecPkgContext_ApplicationProtocol mAlpnResult {}; // The ALPN selected result
    bool mIsHandshakeDone = false;
    bool mIsShutdown = false;
    bool mIsExpired = false;
    bool mVerifyPeer = false;

    // Tls Configure
    std::vector<std::byte> mAlpn;
    std::wstring mHostname;

    // Buffer
    size_t mDecryptedCosume = 0; // The decrypted data consumed in the mReadBuffer
    Buffer mDecryptedBuffer; // The decrypted read buffer
    FixedStreamBuffer<16384 + 100> mReadBuffer;  //< The incoming buffer 2 ** 14 (MAX TLS SIZE) + header + trailer
    FixedStreamBuffer<16384 + 100> mWriteBuffer;

    TlsStateImpl(TlsContextImpl &ctxt) : mCtxt(ctxt), mTable(ctxt.mTable), mVerifyPeer(ctxt.mVerifyPeer) {
        // Mark as not initialized
        SecInvalidateHandle(&mCredHandle);
        SecInvalidateHandle(&mTls);
    }    

    ~TlsStateImpl() {
        if (!mIsShutdown) {
            std::ignore = applyControl(SCHANNEL_SHUTDOWN);
        }
        if (SecIsValidHandle(&mTls)) {
            mTable->DeleteSecurityContext(&mTls);
        }
    }

    // MARK: Handshake
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
            if (!mVerifyPeer) { // Skip peer verification if set
                flags |= ISC_REQ_MANUAL_CRED_VALIDATION;
            }
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

    auto handshakeAsServer(StreamView stream) -> IoTask<void> {
        co_return Err(IoError::Unknown);
    }

    auto handshakeImpl(StreamView stream, TlsRole role) -> IoTask<void> {
        if (!SecIsValidHandle(&mCredHandle)) {
            mCredHandle = mCtxt.credHandle(role);
        }
        if (role == TlsRole::Server) {
            return handshakeAsServer(stream);
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

    // MARK: Read
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

    // MARK: Write
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

} // namespace
} // namespace schannel

// MARK: Export

using namespace schannel;

// Export to user
auto context::make(uint32_t flags) -> void * { // Does it safe for exception?
    return std::make_unique<TlsContextImpl>(flags).release();
}

auto context::destroy(void *ctxt) -> void {
    delete static_cast<TlsContextImpl *>(ctxt);
}

auto context::setVerify(void *ctxt, bool verify) -> void {
    return static_cast<TlsContextImpl *>(ctxt)->setVerify(verify);
}

auto context::loadDefaultRootCerts(void *ctxt) -> bool {
    return static_cast<TlsContextImpl *>(ctxt)->loadDefaultRootCerts();
}

auto context::loadRootCerts(void *ctxt, Buffer buffer) -> bool {
    return static_cast<TlsContextImpl *>(ctxt)->loadRootCerts(buffer);
}

auto context::useCert(void *ctxt, Buffer buffer) -> bool {
    return static_cast<TlsContextImpl *>(ctxt)->useCert(buffer);
}

auto context::usePrivateKey(void *ctxt, Buffer buffer, std::string_view password) -> bool {
    return static_cast<TlsContextImpl *>(ctxt)->usePrivateKey(buffer, password);
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
    if (protocols.empty()) {
        static_cast<TlsStateImpl *>(this)->mAlpn.clear();
        return true;
    }

    // [4 len][4 type][2 len] [1 len][alpn1][alpn2]...
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