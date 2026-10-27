/**
 * @file cmsg.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Control message for socket
 * @version 0.1
 * @date 2026-10-23
 * 
 * @copyright Copyright (c) 2026
 * 
 */
 // Experimental!!!
 // The API maybe changed in the future
#pragma once

#include <ilias/io/system_error.hpp> // IoResult
#include <ilias/net/system.hpp> // CMSG_*
#include <ilias/buffer.hpp> // Buffer
#include <cstdlib> // align_malloc
#include <utility> // std::in_range
#include <memory> // std::unique_ptr
#include <array>
#include <new>

ILIAS_NS_BEGIN

// Import system types
using msghdr_t = ILIAS_MSGHDR_T;
using cmsghdr_t = ILIAS_CMSGHDR_T;

// Some predefined control message types
// MARK: Cmsg
namespace cmsg {

/**
 * @brief The control message can be encoded to an buffer
 * 
 * @tparam T 
 */
template <typename T>
concept EncodeCmsg = requires(const T &t) {
    // Properties
    { T::type() } -> std::convertible_to<int>;
    { T::level() } -> std::convertible_to<int>;

    // Serialization
    { t.encode() } -> std::convertible_to<Buffer>; // Allow std::vector<std::byte> or any can convertible to span<const std::byte>
};

/**
 * @brief The control message can be decoded from buffer
 * 
 * @tparam T 
 */
template <typename T>
concept DecodeCmsg = requires(const T &t) {
    // Properties
    { T::type() } -> std::convertible_to<int>;
    { T::level() } -> std::convertible_to<int>;

    // Deserialization
    { T::decode(Buffer{}) } -> std::same_as<std::optional<T>>;
};

/**
 * @brief The generic control message type
 * 
 * @tparam T 
 */
template <typename T>
concept Cmsg = EncodeCmsg<T> && DecodeCmsg<T>;

/**
 * @brief The cmsg base helper class
 * 
 * @tparam Level 
 * @tparam Type 
 * @tparam T 
 */
template <int Level, int Type, std::default_initializable T> requires(std::is_trivially_copyable_v<T>)
class CmsgT {
public:
    constexpr CmsgT() = default;
    constexpr CmsgT(T value) : mValue{value} {}

    // Properties
    static auto level() noexcept -> int { return Level; }
    static auto type() noexcept -> int { return Type; }

    // Deserialization
    static auto decode(Buffer buffer) -> std::optional<CmsgT> {
        if (buffer.size() != sizeof(T)) {
            return std::nullopt;
        }
        T val{};
        std::memcpy(&val, buffer.data(), sizeof(T));
        return CmsgT{val};
    }

    // Serialization
    auto encode() const -> Buffer {
        return makeBuffer(&mValue, sizeof(T));
    }

    // Get the inner value
    constexpr auto value() const noexcept -> T { return mValue; }
    constexpr operator T() const noexcept { return mValue; }
private:
    T mValue {};
};

// MARK: IPPROTO_IP
using Ipv4Ttl = CmsgT<IPPROTO_IP, IP_TTL, int>;
using Ipv4TosSend = CmsgT<IPPROTO_IP, IP_TOS, int>;
using Ipv4TosRecv = CmsgT<IPPROTO_IP, IP_TOS, uint8_t>;

// MARK: IPPROTO_IPV6
using Ipv6HopLimit = CmsgT<IPPROTO_IPV6, IPV6_HOPLIMIT, int>;
using Ipv6TrafficClass = CmsgT<IPPROTO_IPV6, IPV6_TCLASS, int>;

} // namespace cmsg

// MARK: CmsgSpace
/**
 * @brief The control message buffer storage
 * 
 */
class CmsgSpace {
public:
    static_assert(alignof(cmsghdr_t) <= alignof(std::max_align_t), "The alignment too large, we need to use aligned malloc on this platform");

    explicit CmsgSpace(size_t n) { resize(n); }
    CmsgSpace(CmsgSpace &&) = default;
    CmsgSpace() = default;

    /**
     * @brief Set the new size of the buffer, it may trigger reallocation and cause the pointer change.
     * 
     * @param size 
     */
    auto resize(size_t size) -> void {
        // In windows, check the size is not too large
#if defined(_WIN32)
        ILIAS_ASSERT(std::in_range<decltype(WSABUF::len)>(size), "The size {} is too large, can't passed by ABI", size);
#endif // _WIN32

        if (size == 0) {
            mBuffer.reset();
            mSize = 0;
            return;
        }
        auto *ptr = static_cast<std::byte*>(std::realloc(mBuffer.get(), size));
        if (!ptr) [[unlikely]] {
            ILIAS_THROW(std::bad_alloc{});
        }
        mBuffer.release();
        mBuffer.reset(ptr);
        mSize = size;
    }

    /**
     * @brief Get the writable bytes of the control buffer storage
     * 
     * @return MutableBuffer
     */
    auto data() -> MutableBuffer {
        return {mBuffer.get(), mSize};
    }

    /**
     * @brief Get the size of the buffer storage
     * 
     * @return size_t 
     */
    auto size() const noexcept -> size_t {
        return mSize;
    }

    // Operator
    auto operator =(CmsgSpace &&) -> CmsgSpace & = default;
private:
    struct Deleter {
        auto operator()(std::byte *ptr) const noexcept -> void {
            std::free(ptr);
        }
    };
    std::unique_ptr<std::byte[], Deleter> mBuffer; // Maybe we need to use aligned malloc? aligof(cmsghdr_t)
    size_t mSize = 0;
};

/**
 * @brief The writer uded to write the control message to the space
 * 
 * @code
 *   MsgHdr hdr;
 *   CmsgSpace space;
 *   auto control = CmsgWriter{space}.
 *     .append(cmsg::ScmRights{fd})
 *     .append(cmsg::ScmRights{fd2})
 *     .end();
 *   hdr.setControl(control);
 *   co_await sendmsg(hdr);
 * @endcode
 */
class CmsgWriter {
public:
    explicit CmsgWriter(CmsgSpace &space) : mSpace(space) {}
    CmsgWriter(CmsgWriter &&) = default;

    /**
     * @brief Append an control message to the buffer
     * 
     * @tparam T 
     * @param msg 
     * @return CmsgWriter& 
     */
    template <cmsg::EncodeCmsg T>
    auto append(const T &msg) -> CmsgWriter & {
        auto encoded = msg.encode();
        auto data = makeBuffer(encoded); // The encoded may a vector, cast it to view
        auto spaceSize = ILIAS_CMSG_SPACE(data.size());

        // Check if there is enough space
        if (mSpace.size() < mHead + spaceSize) [[unlikely]] {
            mSpace.resize(mHead + spaceSize);
        }

        // Get the storage space and zero it
        auto *storage = mSpace.data().data() + mHead;
        std::memset(storage, 0, spaceSize);

        // Construct the header
        auto *hdr = new (storage) cmsghdr_t {
            .cmsg_len = ILIAS_CMSG_LEN(data.size()),
            .cmsg_level = T::level(),
            .cmsg_type = T::type(),
        };

        // Copy the data
        std::memcpy(ILIAS_CMSG_DATA(hdr), data.data(), data.size());

        // Advance the head
        mHead += spaceSize;
        return *this;
    }

    /**
     * @brief End the writing process and return the buffer for the msghdr
     * 
     * @return Buffer 
     */
    auto end() -> Buffer {
        return mSpace.data().subspan(0, mHead);
    }
private:
    CmsgSpace &mSpace;
    size_t     mHead = 0;
};

/**
 * @brief The control message header.
 * @code
 *   MutableMgsHdr hdr;
 *   hdr.setControl(space.data());
 *   co_await recvmsg(hdr);
 *   for (const auto &msg : CmsgReader{hdr}) {
 *      if (auto right = msg.get<cmsg::ScmRights>()) {
 *          useRight(*right)
 *      }
 *   }
 * @endcode
 * 
 */
class CmsgReader {
public:
    class View;
    class Iterator;

    explicit CmsgReader(const msghdr_t &msg) : mMsg(msg) {}
    CmsgReader(CmsgReader &&) = default;

    auto begin() const noexcept -> Iterator;
    auto end() const noexcept -> Iterator;
private:
    const msghdr_t &mMsg;
};

/**
 * @brief The view of the control message header
 * 
 */
class CmsgReader::View {
public:
    explicit View(const cmsghdr_t *hdr) : mHdr(hdr) {}
    View(const View &) = default;
    View() = default;

    /**
     * @brief Check the current view is hold the given type
     * 
     * @tparam T 
     * @return true 
     * @return false 
     */
    template <cmsg::DecodeCmsg T>
    auto hold() const noexcept -> bool {
        ILIAS_ASSERT(mHdr);
        return T::level() == mHdr->cmsg_level && T::type() == mHdr->cmsg_type;
    }

    /**
     * @brief Try decode the control message from this header
     * 
     * @tparam T 
     * @return std::optional<T> 
     */
    template <cmsg::DecodeCmsg T>
    auto decode() const -> std::optional<T> {
        ILIAS_ASSERT(mHdr);
        if (!hold<T>()) {
            return std::nullopt;
        }

        Buffer buf {
            reinterpret_cast<const std::byte*>(ILIAS_CMSG_DATA(mHdr)),
            mHdr->cmsg_len - ILIAS_CMSG_LEN(0),
        };
        return T::decode(buf);
    }

    /**
     * @brief Get the inner cmsghdr_t*
     * 
     * @return const cmsghdr_t* 
     */
    auto get() const noexcept -> const cmsghdr_t * {
        return mHdr;
    }

    auto operator =(const View &) -> View & = default;
    auto operator <=>(const View &other) const = default;
private:
    const cmsghdr_t *mHdr = nullptr;
};

/**
 * @brief Iterator of the control message header
 * 
 */
class CmsgReader::Iterator {
public:
    explicit Iterator(const msghdr_t *msg, const cmsghdr_t *cur) : mMsg(msg), mCur(cur) {}
    Iterator(const Iterator &) = default;
    Iterator() = default;

    // Operator
    auto operator ->() const noexcept -> const View * {
        return &mCur;
    }

    auto operator *() const noexcept -> const View & {
        return mCur;
    }

    // Advance
    auto operator ++() noexcept -> Iterator & {
        mCur = View{ILIAS_CMSG_NXTHDR(mMsg, mCur.get())};
        return *this;
    }

    auto operator ++(int) noexcept -> Iterator {
        auto ret = *this;
        ++(*this);
        return ret;
    }

    auto operator =(const Iterator &) -> Iterator & = default;
    auto operator <=>(const Iterator &other) const = default;
private:
    const msghdr_t *mMsg = nullptr;
    View            mCur;
};

inline auto CmsgReader::begin() const noexcept -> Iterator {
    return Iterator {
        &mMsg,
        ILIAS_CMSG_FIRSTHDR(&mMsg),
    };
}

inline auto CmsgReader::end() const noexcept -> Iterator {
    return Iterator {
        &mMsg,
        nullptr,
    };
}

ILIAS_NS_END