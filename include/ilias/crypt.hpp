/**
 * @file crypt.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The crypt wrapper for the bcrypt (Windows) or OpenSSL (other platform)
 * @version 0.1
 * @date 2025-02-27
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <ilias/buffer.hpp>
#include <string>
#include <vector>
#include <array>
#include <span>
#include <bit>

#if defined(_WIN32)
    #define _WINSOCKAPI_ // Avoid windows.h to include winsock.h
    #define NOMINMAX
    #include <Windows.h>
    #include <bcrypt.h>
    #include <VersionHelpers.h>

    #if defined(_MSC_VER)
        #pragma comment(lib, "bcrypt.lib")
    #endif // defined(_MSC_VER)
#else
    #include <openssl/evp.h>
#endif // defined(_WIN32)

ILIAS_NS_BEGIN

namespace base64 {

/**
 * @brief The chars table for base64 encoding
 * 
 */
inline constexpr auto chars = std::string_view {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

/**
 * @brief The chars table for base64 decoding
 * 
 */
inline constexpr auto invchars = []() consteval {
    std::array<std::byte, 256> ret;
    for (size_t i = 0; i < ret.size(); ++i) {
        ret[i] = std::byte{0xff};
    }
    for (size_t i = 0; i < chars.size(); ++i) {
        ret[chars[i]] = std::byte(i);
    }
    // = as 0
    ret['='] = std::byte{0};
    return ret;
}();

template <typename T>
inline constexpr auto failure(T n) noexcept -> T {
    if (std::is_constant_evaluated()) {
        ::abort();
    }
    return n;
}

/**
 * @brief Get the length of the encoded data
 * 
 * @param data The data to encode
 * @return size_t 
 */
inline constexpr auto encodeLength(std::span<const std::byte> data) noexcept -> size_t {
    return (data.size() + 2) / 3 * 4;
}

/**
 * @brief Encode the data to base64
 * 
 * @param in The input data
 * @param out The output buffer (must be at least encodeLength(in) bytes long)
 * @return size_t The length of the encoded data
 */
inline constexpr auto encodeTo(std::span<const std::byte> in, std::span<char> out) noexcept -> size_t {
    if (out.size() < encodeLength(in)) {
        return failure(0);
    }
    size_t idx = 0;
    size_t outIdx = 0;

    for (; idx + 3 <= in.size(); idx += 3) {
        uint32_t tmp = 
            std::to_integer<uint32_t>(in[idx    ]) << 16 |
            std::to_integer<uint32_t>(in[idx + 1]) <<  8 |
            std::to_integer<uint32_t>(in[idx + 2]);
        out[outIdx++] = chars[(tmp >> 18) & 0x3f];
        out[outIdx++] = chars[(tmp >> 12) & 0x3f];
        out[outIdx++] = chars[(tmp >> 6) & 0x3f];
        out[outIdx++] = chars[tmp & 0x3f];
    }

    if (idx < in.size()) { // Padding =
        uint32_t tmp = 0;
        tmp = std::to_integer<uint32_t>(in[idx]) << 16;
        if (idx + 1 < in.size()) {
            tmp |= std::to_integer<uint32_t>(in[idx + 1]) << 8;
        }

        out[outIdx++] = chars[(tmp >> 18) & 0x3f];
        out[outIdx++] = chars[(tmp >> 12) & 0x3f];

        if (idx + 1 < in.size()) {
            out[outIdx++] = chars[(tmp >> 6) & 0x3f];
        }
        else {
            out[outIdx++] = '=';
        }
        out[outIdx++] = '=';
    }
    return outIdx;
}

/**
 * @brief Encode the data to base64
 * 
 * @tparam T The container type to store the encoded data
 * @param in 
 * @return T 
 */
template <MemContainer T = std::string>
inline constexpr auto encode(std::span<const std::byte> in) -> T {
    T buf;
    buf.resize(encodeLength(in));
    auto n = encodeTo(in, buf);
    if (n != encodeLength(in)) {
        ::abort(); // Impossible
    }
    return buf;
}

/**
 * @brief Get the length of the decoded data
 * 
 * @param encoded The encoded string
 * @return size_t 
 */
inline constexpr auto decodeLength(std::string_view encoded) noexcept -> size_t {
    if (encoded.size() % 4 != 0) {
        return failure(0);
    }
    size_t padding = 0;
    if (!encoded.empty()) {
        if (encoded.back() == '=') padding++;
        if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') padding++;
    }
    return (encoded.size() * 3) / 4 - padding;
}

/**
 * @brief Decode the base64 encoded data to binary
 * 
 * @param in 
 * @param out 
 * @return size_t 
 */
inline constexpr auto decodeTo(std::string_view in, std::span<std::byte> out) noexcept -> size_t {
    if (in.size() % 4 != 0) {
        return failure(0);
    }
    if (out.size() < decodeLength(in)) {
        return failure(0);
    }
    size_t idx = 0;
    size_t outIdx = 0;

    for (; idx + 4 <= in.size(); idx += 4) {
        // Check all chars are valid
        auto a = invchars[std::bit_cast<uint8_t>(in[idx    ])];
        auto b = invchars[std::bit_cast<uint8_t>(in[idx + 1])];
        auto c = invchars[std::bit_cast<uint8_t>(in[idx + 2])];
        auto d = invchars[std::bit_cast<uint8_t>(in[idx + 3])];

        if (in[idx] == '=' || in[idx + 1] == '=') { // First two chars can't be padding
            return failure(0);
        }
        if (a == std::byte{0xff} || b == std::byte{0xff} || c == std::byte{0xff} || d == std::byte{0xff}) {
            return failure(0);
        }

        uint32_t tmp =
            std::to_integer<uint32_t>(a) << 18 |
            std::to_integer<uint32_t>(b) << 12 |
            std::to_integer<uint32_t>(c) << 6 |
            std::to_integer<uint32_t>(d);

        out[outIdx++] = std::byte{(tmp >> 16) & 0xff};
        if (in[idx + 2] != '=') {
            out[outIdx++] = std::byte{(tmp >> 8) & 0xff};
        }
        if (in[idx + 3] != '=') {
            out[outIdx++] = std::byte{tmp & 0xff};
        }
    }
    return outIdx;
}

/**
 * @brief Decode the base64 encoded string to a container
 * 
 * @tparam T 
 * @param in 
 * @return T 
 */
template <MemContainer T = std::vector<std::byte> >
inline constexpr auto decode(std::string_view in) -> T {
    T buf;
    buf.resize(decodeLength(in));
    auto n = decodeTo(in, makeBuffer(buf));
    buf.resize(n);
    return buf;
}

} // namespace base64

#if defined(_WIN32)
namespace bcrypt {

/**
 * @brief The CryptoHash class like Md5, Sha1, Sha256, Sha512
 * 
 */
class CryptoHash {
public:
    enum Type {
        Sha1,
        Sha256,
        Sha512,
        Md5,
        Md4,
        NumTypes // Number of types, Internal use only
    };

    CryptoHash(Type type) {
        constexpr auto table = []() consteval {
            std::array<const wchar_t *, NumTypes> table {};
            table[Sha1] = BCRYPT_SHA1_ALGORITHM;
            table[Sha256] = BCRYPT_SHA256_ALGORITHM;
            table[Sha512] = BCRYPT_SHA512_ALGORITHM;
            table[Md5] = BCRYPT_MD5_ALGORITHM;
            table[Md4] = BCRYPT_MD4_ALGORITHM;
            return table;
        }();
        auto status = ::BCryptOpenAlgorithmProvider(&mHandle, table[static_cast<size_t>(type)], nullptr, 0);
        ILIAS_ASSERT(status == 0);
        status = ::BCryptCreateHash(mHandle, &mHashHandle, nullptr, 0, nullptr, 0, ::IsWindows8OrGreater() ? BCRYPT_HASH_REUSABLE_FLAG : 0);
        ILIAS_ASSERT(status == 0);
    }

    CryptoHash(const CryptoHash &) = delete;

    CryptoHash(CryptoHash &&other) : 
        mHandle(std::exchange(other.mHandle, nullptr)),
        mHashHandle(std::exchange(other.mHashHandle, nullptr)) 
    {

    }

    ~CryptoHash() {
        if (mHashHandle) {
            ::BCryptDestroyHash(mHashHandle);
        }
        if (mHandle) {
            ::BCryptCloseAlgorithmProvider(mHandle, 0);
        }
    }

    /**
     * @brief Add data to the hash
     * 
     * @param data 
     */
    auto addData(std::span<const std::byte> data) -> void {
        auto status = ::BCryptHashData(mHashHandle, (PUCHAR) data.data(), data.size(), 0);
        ILIAS_ASSERT(status == 0);
    }

    /**
     * @brief Reset the object, clear the previous result, and ready to add new data
     * 
     */
    auto reset() -> void {
        if (::IsWindows8OrGreater()) {
            return; // BCRYPT_HASH_REUSABLE_FLAG is available from Windows 8
        }
        if (mHashHandle) {
            ::BCryptDestroyHash(mHashHandle);
        }
        auto status = ::BCryptCreateHash(mHandle, &mHashHandle, nullptr, 0, nullptr, 0, 0);
        ILIAS_ASSERT(status == 0);
    }

    /**
     * @brief Get the hash of the result
     * 
     * @param out The buffer to store the hash (size must as same as hashLength)
     */
    auto result(std::span<std::byte> out) -> void {
        auto status = ::BCryptFinishHash(mHashHandle, (PUCHAR) out.data(), out.size(), 0);
        ILIAS_ASSERT(status == 0);
    }

    /**
     * @brief Get the length of the hash
     * 
     * @return size_t 
     */
    auto resultLength() -> size_t {
        DWORD length = 0;
        auto status = ::BCryptGetProperty(mHashHandle, BCRYPT_HASH_LENGTH, (PUCHAR) &length, sizeof(length), &length, 0);
        ILIAS_ASSERT(status == 0);
        return length;
    }

    /**
     * @brief Get the hash of the result
     * 
     * @tparam T 
     * @return T The hash of the data
     */
    template <MemContainer T = std::vector<std::byte> >
    auto result() -> T {
        T buf;
        buf.resize(resultLength());
        result(buf);
        return buf;
    }

    /**
     * @brief Get the hash of the data
     * 
     * @tparam T 
     * @param data The data to hash
     * @param type The hash type
     * @return T 
     */
    template <MemContainer T = std::vector<std::byte> >
    static auto hash(std::span<const std::byte> data, Type type) -> T {
        CryptoHash hash(type);
        hash.addData(data);
        return hash.result<T>();
    }

    /**
     * @brief Get the hash of the data
     * 
     * @param type 
     * @return size_t 
     */
    static auto hashLength(Type type) -> size_t {
        switch (type) {
            case Sha1: return 20;
            case Sha256: return 32;
            case Sha512: return 64;
            case Md5: return 16;
            case Md4: return 16;
            default: ::abort();
        }
    }
private:
    BCRYPT_ALG_HANDLE mHandle = nullptr;
    BCRYPT_HASH_HANDLE mHashHandle = nullptr;
};

} // namespace bcrypt

using bcrypt::CryptoHash;
#endif // defined(_WIN32)

ILIAS_NS_END