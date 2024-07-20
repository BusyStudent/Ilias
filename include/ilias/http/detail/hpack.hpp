#pragma once

#include "../../ilias.hpp"
#include "../../detail/expected.hpp"

#include <vector>
#include <string>
#include <deque>
#include <span>

#define HTTP_2_HPACK_ESTIMATED_OVERHEAD 32
#define HTTP_2_HPACK_INTERGER_REPRESENTATION_PREFIX_N 8

ILIAS_NS_BEGIN

namespace http2::detail {

enum class HpackError : uint32_t {
    Ok,
    InvalidIndexForDecodingTable,
};

class HpackErrorCategory final : public ErrorCategory {
public:
    virtual auto message(int64_t value) const -> std::string override;
    virtual auto name() const -> std::string_view override;
    virtual auto equivalent(int64_t self, const Error &other) const -> bool override;
    static auto  instance() -> const HpackErrorCategory &;
};

auto HpackErrorCategory::message(int64_t value) const -> std::string {
    return std::to_string(value);
}
auto HpackErrorCategory::name() const -> std::string_view {
    return "hpack_error";
}
auto HpackErrorCategory::equivalent(int64_t self, const Error &other) const -> bool {
    return other.category().name() == name() && other.value() == self;
}
auto HpackErrorCategory::instance() -> const HpackErrorCategory & {
    static HpackErrorCategory instance;
    return instance;
}

} // namespace http2::detail

ILIAS_DECLARE_ERROR_(http2::detail::HpackError, http2::detail::HpackErrorCategory);

namespace http2::detail {

/**
 * @brief header name/value pair
 *
 */
struct NameValuePair {
    std::string_view headerName;
    std::string_view headerValue;
};

class HpackContext {
public:
    inline HpackContext() {}
    inline HpackContext(const HpackContext &)            = delete;
    inline HpackContext &operator=(const HpackContext &) = delete;
    inline ~HpackContext() {}
    /**
     * @brief Set the Dynamic Table Size
     * Protocols that use HPACK determine the maximum size that the encoder
     * is permitted to use for the dynamic table.  In HTTP/2, this value is
     * determined by the SETTINGS_HEADER_TABLE_SIZE setting (Section 6.5.2).
     * An encoder can choose to use less capacity than this maximum size
     * (see Section 6.3), but the chosen size MUST stay lower than or equal
     * to the maximum set by the protocol.
     * A change in the maximum size of the dynamic table is signaled via a
     * dynamic table size update (see Section 6.3).  This dynamic table size
     * update MUST occur at the beginning of the first header block
     * following the change to the dynamic table size.  In HTTP/2, this
     * follows a settings acknowledgment (see Section 6.5.3 of [HTTP2]).
     * Multiple updates to the maximum table size can occur between the
     * transmission of two header blocks.  In the case that this size is
     * changed more than once in this interval, the smallest maximum table
     * size that occurs in that interval MUST be signaled in a dynamic table
     * size update.  The final maximum size is always signaled, resulting in
     * at most two dynamic table size updates.  This ensures that the
     * decoder is able to perform eviction based on reductions in dynamic
     * table size (see Section 4.3).
     * Whenever the maximum size for the dynamic table is reduced, entries
     * are evicted from the end of the dynamic table until the size of the
     * dynamic table is less than or equal to the maximum size.
     * @param size
     * @return Result<void>
     */
    inline auto setMaxDynamicTableSize(std::size_t size) -> void {
        while (mDynamicHeaderTables.size() > 0 && mDynamicTableSize > size) {
            const auto &[key, value] = mDynamicHeaderTables.back();
            mDynamicTableSize -= (key.size() + value.size() + HTTP_2_HPACK_ESTIMATED_OVERHEAD);
            mDynamicHeaderTables.pop_back();
        }
        mMaxDynamicTableSize = size;
    }
    inline auto staticTableIndexSize() const -> std::size_t { return kStaticHeaderTables.size(); }
    inline auto dynamicTableIndexSize() const -> std::size_t { return mDynamicHeaderTables.size(); }
    inline auto dynamicTableSize() const -> std::size_t { return mDynamicTableSize; }

    /**
     * @brief index key value table
     * The static table and the dynamic table are combined into a single
     * index address space.
     *  Indices between 1 and the length of the static table (inclusive)
     * refer to elements in the static table (see Section 2.3.1).
     * Indices strictly greater than the length of the static table refer to
     * elements in the dynamic table (see Section 2.3.2).  The length of the
     * static table is subtracted to find the index into the dynamic table.
     * Indices strictly greater than the sum of the lengths of both tables
     * MUST be treated as a decoding error.
     *  For a static table size of s and a dynamic table size of k, the
     * following diagram shows the entire valid index address space.
     * <----------  Index Address Space ---------->
     * <-- Static  Table -->  <-- Dynamic Table -->
     * +---+-----------+---+  +---+-----------+---+
     * | 1 |    ...    | s |  |s+1|    ...    |s+k|
     * +---+-----------+---+  +---+-----------+---+
     *                        ^                   |
     *                        |                   V
     *                 Insertion Point      Dropping Point
     * @param index the index (1-based) to look up in the HPACK table
     * @return Result<NameValuePair>
     */
    inline auto indexToNameValuePair(std::size_t index) -> Result<NameValuePair> {
        if (index < 1) {
            return Unexpected(HpackError::InvalidIndexForDecodingTable);
        }
        --index;
        if (index < kStaticHeaderTables.size()) {
            return kStaticHeaderTables[index];
        }
        else if (index - kStaticHeaderTables.size() < mDynamicHeaderTables.size()) {
            auto &[name, value] = mDynamicHeaderTables[index - kStaticHeaderTables.size()];
            return NameValuePair {name, value};
        }
        return Unexpected(HpackError::InvalidIndexForDecodingTable);
    }
    /**
     * @brief Inserts a new entry into the dynamic table.
     * The dynamic table consists of a list of header fields maintained in
     * first-in, first-out order.  The first and newest entry in a dynamic
     * table is at the lowest index, and the oldest entry of a dynamic table
     * is at the highest index.
     * The dynamic table is initially empty.  Entries are added as each
     * header block is decompressed.
     * The dynamic table can contain duplicate entries (i.e., entries with
     * the same name and same value).  Therefore, duplicate entries MUST NOT
     * be treated as an error by a decoder.
     * The encoder decides how to update the dynamic table and as such can
     * control how much memory is used by the dynamic table.  To limit the
     * memory requirements of the decoder, the dynamic table size is
     * strictly bounded (see Section 4.2).
     * The decoder updates the dynamic table during the processing of a list
     * of header field representations (see Section 3.2).
     * Before a new entry is added to the dynamic table, entries are evicted
     * from the end of the dynamic table until the size of the dynamic table
     * is less than or equal to (maximum size - new entry size) or until the
     * table is empty.
     * If the size of the new entry is less than or equal to the maximum
     * size, that entry is added to the table.  It is not an error to
     * attempt to add an entry that is larger than the maximum size; an
     * attempt to add an entry larger than the maximum size causes the table
     * to be emptied of all existing entries and results in an empty table.
     * A new entry can reference the name of an entry in the dynamic table
     * that will be evicted when adding this new entry into the dynamic
     * table.  Implementations are cautioned to avoid deleting the
     * referenced name if the referenced entry is evicted from the dynamic
     * table prior to inserting the new entry.
     * @param name
     * @param value
     */
    inline auto addNameValuePair(const std::string &name, const std::string &value) -> void {
        auto itemSize = name.size() + value.size() + HTTP_2_HPACK_ESTIMATED_OVERHEAD;
        if (itemSize >= mMaxDynamicTableSize) {
            mDynamicHeaderTables.clear();
            mDynamicTableSize = 0;
            return;
        }
        while (mDynamicHeaderTables.size() > 0 && itemSize + mDynamicTableSize > mMaxDynamicTableSize) {
            const auto &[key, value] = mDynamicHeaderTables.back();
            mDynamicTableSize -= (key.size() + value.size() + HTTP_2_HPACK_ESTIMATED_OVERHEAD);
            mDynamicHeaderTables.pop_back();
        }
        mDynamicTableSize += itemSize;
        mDynamicHeaderTables.emplace_front(name, value);
    }
    inline void setIntegerPrefixLength(int length) noexcept { mIntegerPrefixLength = length; }
    inline int  integerPrefixLength() const noexcept { return mIntegerPrefixLength; }

private:
    constexpr static const std::array<NameValuePair, 61> kStaticHeaderTables =
#include "static_table.inc"
        ; // define static table in static_table.inc
    std::deque<std::pair<std::string, std::string>> mDynamicHeaderTables;
    std::size_t                                     mMaxDynamicTableSize = -1;
    std::size_t                                     mDynamicTableSize    = 0;
    std::size_t mIntegerPrefixLength = HTTP_2_HPACK_INTERGER_REPRESENTATION_PREFIX_N;
};

class HpackDecoder {
public:
    inline HpackDecoder(HpackContext &context) : mContext(context) {}
    inline HpackDecoder(HpackContext &context, std::span<std::byte> buffer, const int offset = 0)
        : mContext(context), mBuffer(buffer), mOffset(offset) {}

    void resetBuffer(std::span<std::byte> buffer, const int offset = 0) {
        mBuffer = buffer;
        mOffset = offset;
    }
    int offset() const {
        return mOffset;
    }
    void setOffset(const int offset) {
        mOffset = offset;
    }

    /**
     * @brief read a integer from buffer
     * Integers are used to represent name indexes, header field indexes, or
     * string lengths.  An integer representation can start anywhere within
     * an octet.  To allow for optimized processing, an integer
     * representation always finishes at the end of an octet.
     * An integer is represented in two parts: a prefix that fills the
     * current octet and an optional list of octets that are used if the
     * integer value does not fit within the prefix.  The number of bits of
     * the prefix (called N) is a parameter of the integer representation.
     * If the integer value is small enough, i.e., strictly less than 2^N-1,
     * it is encoded within the N-bit prefix.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | ? | ? | ? |       Value       |
     * +---+---+---+-------------------+
     * Otherwise, all the bits of the prefix are set to 1, and the value,
     * decreased by 2^N-1, is encoded using a list of one or more octets.
     * The most significant bit of each octet is used as a continuation
     * flag: its value is set to 1 except for the last octet in the list.
     * The remaining bits of the octets are used to encode the decreased
     * value.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | ? | ? | ? | 1   1   1   1   1 |
     * +---+---+---+-------------------+
     * | 1 |    Value-(2^N-1) LSB      |
     * +---+---------------------------+
     *                ...
     * +---+---------------------------+
     * | 0 |    Value-(2^N-1) MSB      |
     * +---+---------------------------+
     * @return int the next position if successful, -1 otherwise
     */
    template <typename T>
    int getInt(T &value) const {
        ILIAS_ASSERT(mContext.integerPrefixLength() <= 8 && mContext.integerPrefixLength() > 0);
        ILIAS_ASSERT(mOffset < mBuffer.size());

        std::byte b {static_cast<unsigned char>((1U << mContext.integerPrefixLength()) - 1U)};
        if (static_cast<unsigned char>(mBuffer[mOffset] & b) < static_cast<unsigned char>(b)) {
            value = static_cast<T>(mBuffer[mOffset] & b);
            return mOffset + 1;
        }
        int current = mOffset + 1, mBitsOffset = 0;
        value = 0;
        while (current < mBuffer.size() && (static_cast<unsigned char>(mBuffer[current]) & 0b10000000U)) {
            value |= static_cast<T>(static_cast<unsigned char>(mBuffer[current]) & 0b01111111U) << mBitsOffset;
            mBitsOffset += 7;
            if (mBitsOffset + 7 > sizeof(T) * 8) {
                return -1;
            }
            ++current;
        }
        if (current < mBuffer.size()) {
            value |= static_cast<T>(static_cast<unsigned char>(mBuffer[current]) & 0b01111111U) << mBitsOffset;
            return current + 1;
        }
        // FIXME: if current == buffer.size() then is error ?
        return -1;
    }

    int getString(std::string &value) const {
        ILIAS_ASSERT(mOffset < mBuffer.size());
        bool isHuffman = static_cast<unsigned char>(mBuffer[mOffset]) & 0b10000000U;
        
    }

private:


private:
    HpackContext        &mContext;
    std::span<std::byte> mBuffer;
    std::size_t          mOffset = 0;
};

class HpackEncoder {
public:
    inline HpackEncoder(HpackContext &context) : mContext(context) {}

private:
    HpackContext &mContext;
};

} // namespace http2::detail

ILIAS_NS_END
