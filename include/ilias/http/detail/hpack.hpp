#pragma once

#include "../../ilias.hpp"
#include "../../detail/expected.hpp"
#include "huffman.hpp"
#include "integer.hpp"

#include <vector>
#include <string>
#include <deque>
#include <span>

#define HTTP_2_HPACK_ESTIMATED_OVERHEAD 32

ILIAS_NS_BEGIN

namespace http2::detail {

enum class HpackError : uint32_t {
    Ok,
    InvalidIndexForDecodingTable,
    IntegerOverflow,
    NeedMoreData,
    InvalidHuffmanEncodedData,
    IndexParserError,
    IndexOutOfRange,

    UnknownError = 0x7FFFFFFF
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

private:
    constexpr static const std::array<NameValuePair, 61> kStaticHeaderTables =
#include "static_table.inc"
        ; // define static table in static_table.inc
    std::deque<std::pair<std::string, std::string>> mDynamicHeaderTables;
    std::size_t                                     mMaxDynamicTableSize = -1;
    std::size_t                                     mDynamicTableSize    = 0;
};

class HpackDecoder {
public:
    inline HpackDecoder(HpackContext &context) : mContext(context) {}
    inline HpackDecoder(HpackContext &context, std::span<const std::byte> buffer, const int offset = 0)
        : mContext(context) {}

    inline auto decode(std::span<const std::byte> buffer) -> Result<int> {}

    inline auto IndexedHeaderField(std::span<const std::byte> buffer) -> Result<int> {
        int  index  = -1;
        auto offset = getInt(buffer, index, 7);
        if (!offset) {
            return Unexpected(HpackError::IndexParserError);
        }
        if (index < 0 || offset.value() < 0) {
            return Unexpected(HpackError::IndexParserError);
        }
        if (index == 0 || index > mContext.staticTableIndexSize() + mContext.dynamicTableIndexSize()) {
            return Unexpected(HpackError::IndexOutOfRange);
        }
        auto ret = mContext.indexToNameValuePair(index);
        if (!ret) {
            return Unexpected(ret.error());
        }
        mDecodeHeaderList.push_back(ret.value());
        return offset.value();
    }

    inline auto LiteralHeaderFieldWithIncrementalIndexing(std::span<const std::byte> buffer) -> Result<void> {
        int  index  = -1;
        auto offset = getInt(buffer, index, 6);
        if (!offset) {
            return Unexpected(HpackError::IndexParserError);
        }
        if (index < 0) {
            return Unexpected(HpackError::IndexParserError);
        }
        if (index == 0) {
            std::string name, value;
            auto        toffset = getString(buffer, name);
            // TODO:
        }
    }

    /**
     * @brief read a integer from buffer
     * @return int the next position if successful, -1 otherwise
     */
    template <typename T>
    inline auto getInt(std::span<const std::byte> buffer, T &value, const int allowPrefixBits = 8) const
        -> Result<int> {
        ILIAS_ASSERT(buffer.size() > 0);
        ILIAS_ASSERT(allowPrefixBits <= 8);
        auto ret = IntegerDecoder::decode(buffer, value, 8 - allowPrefixBits);
        if (ret < 0) {
            return Unexpected(ret == -1 ? HpackError::IntegerOverflow : HpackError::NeedMoreData);
        }
        return ret;
    }

    inline auto getString(std::span<const std::byte> buffer, std::string &value) const -> Result<int> {
        ILIAS_ASSERT(buffer.size() > 0);
        bool        isHuffman = static_cast<unsigned char>(buffer[0]) & 0b10000000U;
        std::size_t length;
        auto        ret = getInt(buffer, length, 7);
        if (!ret) {
            return Unexpected(ret.error());
        }
        auto length_length = ret.value();
        if (length_length < 0 || length_length + length > buffer.size()) {
            return Unexpected(HpackError::NeedMoreData);
        }
        if (isHuffman) {
            std::vector<std::byte> decoded;
            auto decode_length = HuffmanDecoder::decode(buffer.subspan(length_length, length), decoded);
            if (decode_length != length) {
                return Unexpected(HpackError::InvalidHuffmanEncodedData);
            }
            value = std::string(reinterpret_cast<const char *>(decoded.data()), decoded.size());
            return decode_length + length_length;
        }
        else {
            value = std::string(reinterpret_cast<const char *>(buffer.data() + length_length), length);
            return length + length_length;
        }
    }

private:
    HpackContext              &mContext;
    std::vector<NameValuePair> mDecodeHeaderList;
};

class HpackEncoder {
public:
    inline HpackEncoder(HpackContext &context) : mContext(context) {}

    template <typename T>
    inline auto saveInt(T &&value, const int allowPrefixBits = 8) -> Result<void> {
        ILIAS_ASSERT(allowPrefixBits <= 8);
        auto ret = IntegerEncoder::encode(std::forward<T>(value), mBuffer, 8 - allowPrefixBits);
        if (ret < 0) {
            return Unexpected(HpackError::UnknownError);
        }
        return Result<void> {};
    }

    inline auto saveString(const std::string &value, const bool huffmanEncoding = false) -> Result<void> {
        if (huffmanEncoding) {
            std::vector<std::byte> buffer;

            if (HuffmanEncoder::encode({reinterpret_cast<const std::byte *>(value.data()), value.size()}, buffer) !=
                0) {
                return Unexpected(HpackError::UnknownError);
            }
            mBuffer.push_back(static_cast<std::byte>(0x80));
            saveInt(buffer.size(), 7);
            mBuffer.insert(mBuffer.end(), buffer.begin(), buffer.end());
            return Result<void> {};
        }
        else {
            mBuffer.push_back(static_cast<std::byte>(0x00));
            saveInt(value.size(), 7);
            mBuffer.insert(mBuffer.end(), reinterpret_cast<const std::byte *>(value.data()),
                           reinterpret_cast<const std::byte *>(value.data()) + value.size());
            return Result<void> {};
        }
    }

    void reset() { mBuffer.clear(); }

    std::vector<std::byte>       &buffer() { return mBuffer; }
    const std::vector<std::byte> &buffer() const { return mBuffer; }
    std::size_t                   size() const { return mBuffer.size(); }

private:
    HpackContext          &mContext;
    std::vector<std::byte> mBuffer;
};

} // namespace http2::detail

ILIAS_NS_END
