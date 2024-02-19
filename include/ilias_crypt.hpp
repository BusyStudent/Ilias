#pragma once

#include <vector>
#include "ilias.hpp"

#ifdef _WIN32
    #include <Windows.h>
    #include <bcrypt.h>
#endif


ILIAS_NS_BEGIN

class CryptographicHash {
public:
    enum Algorithm {
        Sha1,
        Sha256,
        Sha512
    };
    /**
     * @brief Construct a new Cryptographic Hash object
     * 
     * @param algo 
     */
    explicit CryptographicHash(Algorithm algo);
    CryptographicHash(const CryptographicHash&) = delete;
    ~CryptographicHash();

    /**
     * @brief Reset the state
     * 
     */
    void reset();
    /**
     * @brief Add more data into 
     * 
     * @param data 
     * @param len 
     */
    void addData(const void* data, size_t len);

    /**
     * @brief Get the result
     * 
     * @param data 
     * @param length 
     */
    void resultView(void **data, size_t *length) const;
    /**
     * @brief Get the algorithm
     * 
     * @return Algorithm 
     */
    Algorithm algorithm() const;

    /**
     * @brief Check a algorithm is supported
     * 
     * @param algorithm 
     * @return true 
     * @return false 
     */
    static bool supportsAlgorithm(Algorithm algorithm);
    /**
     * @brief Get the hash value of the data
     * 
     * @param algorithm 
     * @param data 
     * @param len 
     * @return std::vector<uint8_t> 
     */
    static std::vector<uint8_t> hash(Algorithm algorithm, const void *data, size_t len);
private:
    Algorithm mAlgorithm;
    BCRYPT_ALG_HANDLE  mHandle = nullptr;
    BCRYPT_HASH_HANDLE mHashHandle = nullptr;
};

inline CryptographicHash::CryptographicHash(Algorithm algorithm) : mAlgorithm(algorithm) {
    constexpr std::pair<Algorithm, LPCWSTR> algorithms[] = {
        {Algorithm::Sha1, BCRYPT_SHA1_ALGORITHM},
        {Algorithm::Sha256, BCRYPT_SHA256_ALGORITHM},
        {Algorithm::Sha512, BCRYPT_SHA256_ALGORITHM},
    };
    ::BCryptOpenAlgorithmProvider(&mHandle, algorithms[static_cast<size_t>(algorithm)].second, nullptr, 0);
    ::BCryptCreateHash(&mHandle, &mHashHandle, nullptr, 0, nullptr, 0, 0);
}
inline CryptographicHash::~CryptographicHash() {
    if (mHashHandle) {
        ::BCryptDestroyHash(mHashHandle);
    }
    if (mHandle) {
        ::BCryptCloseAlgorithmProvider(mHandle, 0);
    }
}

inline std::vector<uint8_t> CryptographicHash::hash(Algorithm algorithm, const void *data, size_t len) {
    CryptographicHash h(algorithm);
    void *resultData = nullptr;
    size_t resultLength = 0;
    h.addData(data, len);
    h.resultView(&resultData, &resultLength);

    std::vector<uint8_t> result(resultLength);
    ::memcpy(result.data(), resultData, resultLength);
    return result;
}

ILIAS_NS_END