/**
 * @file iovec.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The system abi compatible IoVec
 * @version 0.1
 * @date 2025-08-13
 * 
 * @copyright Copyright (c) 2025
 * 
 */
// Experimental!!!
#pragma once

#include <ilias/defines.hpp>
#include <ilias/buffer.hpp>
#include <cstddef> // size_t
#include <cstdlib> // malloc
#include <limits> // std::numeric_limits
#include <vector> // std::vector
#include <span> // std::span

#if defined(_WIN32)
    struct _WSABUF; // Forward declaration for Windows
#else
    #include <sys/uio.h> // struct iovec
#endif // _WIN32

ILIAS_NS_BEGIN

namespace detail {

// I don't want to include winsock2.h here, so i take this from winsock2.h
struct WinIoVec {
    unsigned long iov_len; // The name as same as linux struct iovec
    void *iov_base;
};

#if defined(_WIN32)
    using IoVecBase = WinIoVec;
    using IoVecSys  = ::_WSABUF;
#else
    using IoVecBase = ::iovec;
    using IoVecSys  = ::iovec;
#endif // _WIN32

} // namespace detail

/**
 * @brief The byte constant buffer view, (with ABI compatible with iovec or WSABUF)
 * 
 */
class IoVec : public detail::IoVecBase {
public:
    using Base = detail::IoVecBase;

    constexpr IoVec() : Base{} {}
    constexpr IoVec(const IoVec &) = default;
    constexpr IoVec(Buffer buffer) : IoVec{buffer.data(), buffer.size()} {}
    constexpr IoVec(const void *buf, size_t len) : Base{} {
        iov_base = const_cast<void *>(buf);
        iov_len = len;

#if defined(_WIN32) && !defined(NDEBUG)
        // In windows, len just uint32_t, we need to check it
        if (len > maxSize()) {
            ILIAS_THROW(std::overflow_error("iov_len is too large"));
        }
#endif // _WIN32
    }

    // Get the data pointer (only for const)
    constexpr auto data() const noexcept -> const std::byte * {
        return static_cast<const std::byte *>(iov_base);
    }

    // Get the size of the buffer
    constexpr auto size() const noexcept -> size_t {
        return iov_len;
    }

    // Check if the buffer is empty
    constexpr auto empty() const noexcept -> bool {
        return iov_len == 0;
    }

    // Compare two IoVec
    constexpr auto operator==(const IoVec &rhs) const noexcept {
        if (empty() && rhs.empty()) return true;
        return iov_len == rhs.iov_len && iov_base == rhs.iov_base;
    }

    // Allow cast to Buffer
    constexpr operator Buffer() const noexcept {
        return {data(), size()};
    }

    // Get the max size allowed by the system
    constexpr static auto maxSize() noexcept -> size_t {
        return std::numeric_limits<decltype(iov_len)>::max();
    }
};

/**
 * @brief The byte mutable buffer view, (with ABI compatible iovec or WSABUF)
 * 
 */
class MutableIoVec final : public IoVec {
public:
    using Base = IoVec;

    constexpr MutableIoVec() : Base{} {}
    constexpr MutableIoVec(const MutableIoVec &) = default;
    constexpr MutableIoVec(const IoVec &) = delete; // Not allow to convert from const to mutable
    constexpr MutableIoVec(void *buf, size_t len) : Base{buf, len} {}
    constexpr MutableIoVec(MutableBuffer buffer) : Base{buffer.data(), buffer.size()} {}

    // Get the data pointer
    constexpr auto data() const noexcept -> std::byte * {
        return static_cast<std::byte *>(iov_base);
    }

    // size & empty are inherited from IoVec
    using IoVec::size;
    using IoVec::empty;

    // Allow cast to MutableBuffer && Buffer
    constexpr operator MutableBuffer() const noexcept {
        return {data(), size()};
    }

    constexpr operator Buffer() const noexcept {
        return {data(), size()};
    }
};

// Sequence of IoVec, used in dyn, it has the BufferSequence concepts
using IoVecSequence        = std::span<const IoVec>;
using MutableIoVecSequence = std::span<const MutableIoVec>;

#if !defined(NDEBUG)
static_assert(BufferSequence<IoVecSequence>);
static_assert(BufferSequence<MutableIoVecSequence>); // Mutable -> Const, OK!
static_assert(MutableBufferSequence<MutableIoVecSequence>);
#endif // NDEBUG

/**
 * @brief Convert IoVec to system raw IoVec pointer
 * 
 * @tparam T 
 * @param iovec The IoVec object pointer
 */
template <typename T> requires (std::is_base_of_v<IoVec, T>)
inline auto toSystem(const T *iovec) noexcept {
    return reinterpret_cast<const detail::IoVecSys *>(iovec);
}

// No const version
template <typename T> requires (std::is_base_of_v<IoVec, T>)
inline auto toSystem(T *iovec) noexcept {
    return reinterpret_cast<detail::IoVecSys *>(iovec);
}

// Convert any BufferSequence to IoVecSequence
template <BufferSequence T>
inline auto makeIoSequence(const T &seq) {
    if constexpr (std::convertible_to<T, IoVecSequence>) {
        return IoVecSequence(seq);
    }
    else if constexpr (std::convertible_to<T, MutableIoVecSequence>) {
        // Mutable -> Const, OK!
        auto mutSeq = MutableIoVecSequence(seq);
        return IoVecSequence(static_cast<const IoVec *>(mutSeq.data()), mutSeq.size());
    }
    else {
        std::vector<IoVec> vec;
        if constexpr (requires { std::size(seq); }) {
            vec.reserve(std::size(seq));
        }
        for (Buffer buf : seq) {
            vec.emplace_back(buf);
        }
        return vec;
    }
}

// Convert any MutableBufferSequence to MutableIoVecSequence
template <MutableBufferSequence T>
inline auto makeMutableIoSequence(const T &seq) {
    if constexpr (std::convertible_to<T, MutableIoVecSequence>) {
        return MutableIoVecSequence(seq);
    }
    else {
        std::vector<MutableIoVec> vec;
        if constexpr (requires { std::size(seq); }) {
            vec.reserve(std::size(seq));
        }
        for (MutableBuffer buf : seq) {
            vec.emplace_back(buf);
        }
        return vec;
    }
}

ILIAS_NS_END