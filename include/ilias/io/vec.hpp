/**
 * @file vec.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The System ABI Compatible IoVec 
 * @version 0.1
 * @date 2025-04-13
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#pragma once

#include <ilias/buffer.hpp>
#include <ilias/ilias.hpp>
#include <limits>

#if defined(_WIN32)
    #define ILIAS_IOVEC_T ::WSABUF
    #define iov_base buf
    #define iov_len  len
    #define NOMINMAX 1
    #include <WinSock2.h>
#else
    #define ILIAS_IOVEC_T ::iovec
    #include <sys/uio.h>
#endif


ILIAS_NS_BEGIN

using iovec_t  = ILIAS_IOVEC_T;

/**
 * @brief Wrapper for system iovec on linux, WSABUF on windows, use it carefully
 * 
 */
class IoVec final : public iovec_t {
public:
    IoVec() = default;
    IoVec(const IoVec &) = default;
    IoVec(const iovec_t &v) : iovec_t(v) { }

    /**
     * @brief Construct a new Io Vec object
     * 
     * @param buf The buffer to be used
     * @param size The size of the buffer
     */
    IoVec(void *buf, size_t size) {
        ILIAS_ASSERT(size < sizeLimit());
        iov_base = static_cast<char *>(buf); //< In Windows this field is a char*
        iov_len = size;
    }

    /**
     * @brief Construct a new Io Vec object from MutableBuffer
     * 
     * @param buffer 
     */
    IoVec(MutableBuffer buffer) {
        ILIAS_ASSERT(buffer.size() < sizeLimit());
        iov_base = reinterpret_cast<char *>(buffer.data());
        iov_len = buffer.size();
    }

    /**
     * @brief Explicit Construct a new Io Vec object from Buffer
     * @note This is need explicitly because of the const_cast
     * @param buffer 
     */
    explicit IoVec(Buffer buffer) {
        ILIAS_ASSERT(buffer.size() < sizeLimit());
        iov_base = const_cast<char *>(reinterpret_cast<const char *>(buffer.data()));
        iov_len = buffer.size();
    }

    /**
     * @brief Get the data pointer
     * 
     * @return std::byte * 
     */
    auto data() const -> std::byte * { return reinterpret_cast<std::byte *>(iov_base); }

    /**
     * @brief Get the size of the buffer
     * 
     * @return size_t 
     */
    auto size() const -> size_t { return iov_len; }

    /**
     * @brief Allow conversion to MutableBuffer
     * 
     * @return MutableBuffer 
     */
    operator MutableBuffer() const noexcept {
        return { data(), size() };
    }

    /**
     * @brief Allow conversion to Buffer
     * 
     * @return Buffer
     */
    operator Buffer() const noexcept {
        return { data(), size() };
    }

    /**
     * @brief Utility function to advance the IoVec Array by the given bytes
     * 
     * @param vecs The mutable span of IoVec
     * @param bytes The number of bytes to advance
     * @return std::span<IoVec> The remaining IoVecs span
     */
    static auto advance(std::span<IoVec> vecs, size_t bytes) -> std::span<IoVec> {
        size_t idx = 0;
        for ( ; idx < vecs.size(); ++idx) {
            auto &vec = vecs[idx];
            if (vec.size() > bytes) { // Current vec has enough bytes
                vec.iov_base = reinterpret_cast<char *>(vec.data() + bytes);
                vec.iov_len -= bytes;
                break;
            }
            bytes -= vec.size(); // Current vec is not enough, consume it

            // Zero out the vec
            vec.iov_base = nullptr;
            vec.iov_len = 0;
        }
        return vecs.subspan(idx);
    }
private:
    /**
     * @brief The platform specific iovec maximum size (windows: uint32, linux: size_t)
     * 
     * @return size_t 
     */
    static auto sizeLimit() -> size_t {
        return std::numeric_limits<decltype(iov_len)>::max();
    }
};

static_assert(sizeof(IoVec) == sizeof(iovec_t));

ILIAS_NS_END


#if defined(_WIN32)
    #undef iov_base
    #undef iov_len
#endif 