#pragma once

#include <ilias/detail/expected.hpp>
#include <ilias/error.hpp>
#include "huffman.hpp"
#include "integer.hpp"

#include <vector>
#include <string>
#include <deque>
#include <span>

#define HTTP_2_HPACK_ESTIMATED_OVERHEAD 32

ILIAS_NS_BEGIN

namespace http2 {

enum class HpackError : uint32_t {
    Ok,
    InvalidIndex,
    IntegerOverflow,
    NeedMoreData,
    InvalidHuffmanEncodedData,
    IndexParserError,
    IndexOutOfRange,
    UnknowHeaderField,
    SizeOutOfLimit,
    FieldNotInTable,
    UnknowFieldType,

    UnknownError = 0x7FFFFFFF
};

class HpackErrorCategory final : public ErrorCategory {
public:
    virtual auto message(int64_t value) const -> std::string override;
    virtual auto name() const -> std::string_view override;
    virtual auto equivalent(int64_t self, const Error &other) const -> bool override;
    static auto  instance() -> const HpackErrorCategory &;
};

ILIAS_DECLARE_ERROR(HpackError, HpackErrorCategory)

enum class HeaderFieldType : uint8_t {
    Indexed             = 0,
    IncrementalIndexing = 1,
    WithoutIndexing     = 3,
    NeverIndexed        = 5,

    Unknow = 0xFF
};

struct HeaderFieldView {
    std::string_view headerName  = {};
    std::string_view headerValue = {};
    HeaderFieldType  type        = HeaderFieldType::Unknow;
};

struct HeaderField {
    std::string     headerName  = {};
    std::string     headerValue = {};
    HeaderFieldType type        = HeaderFieldType::Unknow;

    HeaderField(std::string_view name = "", std::string_view value = "",
                HeaderFieldType type = HeaderFieldType::Unknow);
    HeaderField(HeaderFieldView view);
    HeaderField();
    HeaderField(const HeaderField &)            = default;
    HeaderField &operator=(const HeaderField &) = default;
                 operator HeaderFieldView() const;
};

class HpackContext {
public:
    HpackContext()  = default;
    ~HpackContext() = default;
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
    auto setMaxDynamicTableSize(std::size_t size) -> Result<void>;
    auto maxDynamicTableSize() const noexcept -> std::size_t;
    /**
     * @brief Set the Limit Dynamic Table Size object
     * Limit the maximum dynamic table size, which checks the currently set maximum dynamic table size and applies the
     * setting when setting
     * @param size
     */
    auto setLimitDynamicTableSize(std::size_t size) -> void;
    auto limitDynamicTableSize() const noexcept -> std::size_t;
    /**
     * @brief current size of dynamic table
     *
     * @return std::size_t
     */
    auto dynamicTableSize() const -> std::size_t;
    /**
     * @brief max index of static table
     *
     * @return std::size_t
     */
    auto staticTableIndexSize() const -> std::size_t;
    /**
     * @brief max index of dynamic table
     *
     * @return std::size_t
     */
    auto dynamicTableIndexSize() const -> std::size_t;
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
     * @return Result<HeaderField>
     */
    auto indexToHeaderField(std::size_t index) -> Result<HeaderFieldView>;
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
    auto appendHeaderField(std::string_view name, std::string_view value) -> void;
    auto findHeaderField(std::string_view name, std::string_view value = "") -> Result<std::size_t>;

private:
    HpackContext(const HpackContext &)            = delete;
    HpackContext &operator=(const HpackContext &) = delete;

private:
    constexpr static const std::array<HeaderFieldView, 61> kStaticHeaderTables =
#include "static_table.inc"
        ; // define static table in static_table.inc
    std::deque<HeaderField> mDynamicHeaderTables;
    std::size_t             mMaxDynamicTableSize   = -1;
    std::size_t             mLimitDynamicTableSize = -1;
    std::size_t             mDynamicTableSize      = 0;
};

class HpackDecoder {
    using HuffmanDecoder = detail::HuffmanDecoder;
    using IntegerDecoder = detail::IntegerDecoder;

public:
    HpackDecoder(HpackContext &context);
    HpackDecoder(HpackContext &context, std::span<const std::byte> buffer, const int offset = 0);
    /// @brief decode the buffer to get header field list
    auto decode(std::span<const std::byte> buffer) -> Result<void>;

    /// @brief get decode header field list
    auto headerFieldList() const noexcept -> const std::vector<HeaderField> &;
    /// @brief get decode header field list
    auto headerFieldList() noexcept -> std::vector<HeaderField> &;
    /// @brief clear current header field list
    auto clear() noexcept -> void;

private:
    /**
     * @brief parser a indexed header field
     * An indexed header field representation identifies an entry in either
     * the static table or the dynamic table (see Section 2.3).
     * An indexed header field representation causes a header field to be
     * added to the decoded header list, as described in Section 3.2.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | 1 |        Index (7+)         |
     * +---+---------------------------+
     * An indexed header field starts with the '1' 1-bit pattern, followed
     * by the index of the matching header field, represented as an integer
     * with a 7-bit prefix (see Section 5.1).
     * The index value of 0 is not used.  It MUST be treated as a decoding
     * error if found in an indexed header field representation.
     * @param buffer
     * @return Result<int>
     */
    auto indexedHeaderField(std::span<const std::byte> buffer) -> Result<int>;
    /**
     * @brief literal header field
     * A literal header field representation contains a literal header field
     * value.  Header field names are provided either as a literal or by
     * reference to an existing table entry, either from the static table or
     * the dynamic table (see Section 2.3).
     * This specification defines three forms of literal header field
     * representations: with indexing, without indexing, and never indexed.
     * @par Literal Header Field With Incremental Indexing
     * A literal header field with incremental indexing representation
     * results in appending a header field to the decoded header list and
     * inserting it as a new entry into the dynamic table.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | 0 | 1 |      Index (6+)       |
     * +---+---+-----------------------+
     * | H |     Value Length (7+)     |
     * +---+---------------------------+
     * | Value String (Length octets)  |
     * +-------------------------------+
     * Figure 6: Literal Header Field with Incremental Indexing -- Indexed Name
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | 0 | 1 |           0           |
     * +---+---+-----------------------+
     * | H |     Name Length (7+)      |
     * +---+---------------------------+
     * |  Name String (Length octets)  |
     * +---+---------------------------+
     * | H |     Value Length (7+)     |
     * +---+---------------------------+
     * | Value String (Length octets)  |
     * +-------------------------------+
     * Figure 7: Literal Header Field with Incremental Indexing -- New Name
     * A literal header field with incremental indexing representation
     * starts with the '01' 2-bit pattern.
     * If the header field name matches the header field name of an entry
     * stored in the static table or the dynamic table, the header field
     * name can be represented using the index of that entry.  In this case,
     * the index of the entry is represented as an integer with a 6-bit
     * prefix (see Section 5.1).  This value is always non-zero.
     * Otherwise, the header field name is represented as a string literal
     * (see Section 5.2).  A value 0 is used in place of the 6-bit index,
     * followed by the header field name.
     *  Either form of header field name representation is followed by the
     * header field value represented as a string literal (see Section 5.2).
     * @par Literal Header Field without Indexing
     * A literal header field without indexing representation results in
     * appending a header field to the decoded header list without altering
     * the dynamic table.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | 0 | 0 | 0 | 0 |  Index (4+)   |
     * +---+---+-----------------------+
     * | H |     Value Length (7+)     |
     * +---+---------------------------+
     * | Value String (Length octets)  |
     * +-------------------------------+
     * Figure 8: Literal Header Field without Indexing -- Indexed Name
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | 0 | 0 | 0 | 0 |       0       |
     * +---+---+-----------------------+
     * | H |     Name Length (7+)      |
     * +---+---------------------------+
     * |  Name String (Length octets)  |
     * +---+---------------------------+
     * | H |     Value Length (7+)     |
     * +---+---------------------------+
     * | Value String (Length octets)  |
     * +-------------------------------+
     * Figure 9: Literal Header Field without Indexing -- New Name
     * A literal header field without indexing representation starts with
     * the '0000' 4-bit pattern.
     * If the header field name matches the header field name of an entry
     * stored in the static table or the dynamic table, the header field
     * name can be represented using the index of that entry.  In this case,
     * the index of the entry is represented as an integer with a 4-bit
     * prefix (see Section 5.1).  This value is always non-zero.
     * Otherwise, the header field name is represented as a string literal
     * (see Section 5.2).  A value 0 is used in place of the 4-bit index,
     * followed by the header field name.
     * Either form of header field name representation is followed by the
     * header field value represented as a string literal (see Section 5.2).
     * @par Literal Header Field Never Indexed
     * A literal header field never-indexed representation results in
     * appending a header field to the decoded header list without altering
     * the dynamic table.  Intermediaries MUST use the same representation
     * for encoding this header field.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | 0 | 0 | 0 | 1 |  Index (4+)   |
     * +---+---+-----------------------+
     * | H |     Value Length (7+)     |
     * +---+---------------------------+
     * | Value String (Length octets)  |
     * +-------------------------------+
     * Figure 10: Literal Header Field Never Indexed -- Indexed Name
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | 0 | 0 | 0 | 1 |       0       |
     * +---+---+-----------------------+
     * | H |     Name Length (7+)      |
     * +---+---------------------------+
     * |  Name String (Length octets)  |
     * +---+---------------------------+
     * | H |     Value Length (7+)     |
     * +---+---------------------------+
     * | Value String (Length octets)  |
     * +-------------------------------+
     * Figure 11: Literal Header Field Never Indexed -- New Name
     * A literal header field never-indexed representation starts with the
     * '0001' 4-bit pattern.
     * When a header field is represented as a literal header field never
     * indexed, it MUST always be encoded with this specific literal
     * representation.  In particular, when a peer sends a header field that
     * it received represented as a literal header field never indexed, it
     * MUST use the same representation to forward this header field.
     * This representation is intended for protecting header field values
     * that are not to be put at risk by compressing them (see Section 7.1
     * for more details).
     * The encoding of the representation is identical to the literal header
     * field without indexing (see Section 6.2.2).
     * @param buffer
     * @return Result<int>
     */
    auto literalHeaderField(std::span<const std::byte> buffer, const bool incremental = true) -> Result<int>;
    auto updateDynamicTableSize(std::span<const std::byte> buffer) -> Result<int>;
    /**
     * @brief read a integer from buffer
     *
     * @param[in] buffer
     * @param[out] value the value to read
     * @return Result<int> the next position if successful
     */
    template <typename T>
    auto decodeInt(std::span<const std::byte> buffer, T &value, const int allowPrefixBits = 8) const -> Result<int>;
    /**
     * @brief read the string from buffer
     *
     * @param[in] buffer
     * @param[out] value the value to read
     * @return Result<int> the next position if successful
     */
    auto decodeString(std::span<const std::byte> buffer, std::string &value) const -> Result<int>;

    friend class HpackDecoderTest;

private:
    HpackContext            &mContext;
    std::vector<HeaderField> mDecodeHeaderList;
};

class HpackEncoder {
    using HuffmanEncoder = detail::HuffmanEncoder;
    using IntegerEncoder = detail::IntegerEncoder;

public:
    HpackEncoder(HpackContext &context);
    ~HpackEncoder() = default;
    /// @brief encode the header field list to buffer
    auto encode(const std::vector<HeaderField> &headerList, const bool huffman = true) -> Result<void>;
    /// @brief encode the header field to buffer
    auto encode(HeaderFieldView header, const bool huffman = true) -> Result<void>;
    /// @brief  encode the header field to buffer
    auto encode(std::string_view name, std::string_view value, const HeaderFieldType type = HeaderFieldType::Unknow,
                const bool huffman = true) -> Result<void>;

    /// @brief clear the encode buffer
    auto clear() -> void;
    /// @brief  get the encode buffer
    auto buffer() -> std::vector<std::byte> &;
    /// @brief  get the encode buffer
    auto buffer() const -> const std::vector<std::byte> &;
    /// @brief get encode buffer size
    auto size() const -> std::size_t;

private:
    auto literalHeaderField(std::string_view name, std::string_view value, const bool incremental = true,
                            const bool huffman = true) -> Result<void>;
    auto literalHeaderField(std::size_t name_index, std::string_view value, const bool incremental = true,
                            const bool huffman = true) -> Result<void>;
    auto indexedHeaderField(const std::size_t index) -> Result<void>;
    auto updateDynamicTableSize(const std::size_t index) -> Result<void>;
    template <typename T>
    auto encodeInt(T &&value, const int allowPrefixBits = 8) -> Result<void>;
    auto encodeString(std::string_view value, const bool huffmanEncoding = false) -> Result<void>;

private:
    friend class HpackEncoderTest;

private:
    HpackContext          &mContext;
    std::vector<std::byte> mBuffer;
};

inline HeaderField::HeaderField(std::string_view name, std::string_view value, HeaderFieldType type)
    : headerName(name), headerValue(value), type(type) {
}
inline HeaderField::HeaderField(HeaderFieldView view)
    : headerName(view.headerName), headerValue(view.headerValue), type(HeaderFieldType::Unknow) {
}
inline HeaderField::HeaderField() : headerName(), headerValue(), type(HeaderFieldType::Unknow) {
}

inline HeaderField::operator HeaderFieldView() const {
    return {headerName, headerValue, type};
}

inline auto HpackContext::setMaxDynamicTableSize(std::size_t size) -> Result<void> {
    if (size > mLimitDynamicTableSize) {
        return Unexpected(HpackError::SizeOutOfLimit);
    }
    while (mDynamicHeaderTables.size() > 0 && mDynamicTableSize > size) {
        const auto &[key, value, type] = mDynamicHeaderTables.back();
        mDynamicTableSize -= (key.size() + value.size() + HTTP_2_HPACK_ESTIMATED_OVERHEAD);
        mDynamicHeaderTables.pop_back();
    }
    mMaxDynamicTableSize = size;
    return Result<void> {};
}

inline auto HpackContext::maxDynamicTableSize() const noexcept -> std::size_t {
    return mMaxDynamicTableSize;
}

inline auto HpackContext::setLimitDynamicTableSize(std::size_t size) -> void {
    mLimitDynamicTableSize = size;
    if (mMaxDynamicTableSize > size) {
        setMaxDynamicTableSize(size);
    }
}

inline auto HpackContext::limitDynamicTableSize() const noexcept -> std::size_t {
    return mLimitDynamicTableSize;
}

inline auto HpackContext::staticTableIndexSize() const -> std::size_t {
    return kStaticHeaderTables.size();
}
inline auto HpackContext::dynamicTableIndexSize() const -> std::size_t {
    return mDynamicHeaderTables.size();
}
inline auto HpackContext::dynamicTableSize() const -> std::size_t {
    return mDynamicTableSize;
}

inline auto HpackContext::indexToHeaderField(std::size_t index) -> Result<HeaderFieldView> {
    if (index < 1) {
        return Unexpected(HpackError::InvalidIndex);
    }
    --index;
    if (index < kStaticHeaderTables.size()) {
        return kStaticHeaderTables[index];
    }
    else if (index - kStaticHeaderTables.size() < mDynamicHeaderTables.size()) {
        auto &[name, value, type] = mDynamicHeaderTables[index - kStaticHeaderTables.size()];
        return HeaderFieldView {name, value};
    }
    return Unexpected(HpackError::IndexOutOfRange);
}

inline auto HpackContext::appendHeaderField(std::string_view name, std::string_view value) -> void {
    auto itemSize = name.size() + value.size() + HTTP_2_HPACK_ESTIMATED_OVERHEAD;
    if (itemSize >= mMaxDynamicTableSize) {
        mDynamicHeaderTables.clear();
        mDynamicTableSize = 0;
        return;
    }
    while (mDynamicHeaderTables.size() > 0 && itemSize + mDynamicTableSize > mMaxDynamicTableSize) {
        const auto &[name, value, type] = mDynamicHeaderTables.back();
        mDynamicTableSize -= (name.size() + value.size() + HTTP_2_HPACK_ESTIMATED_OVERHEAD);
        mDynamicHeaderTables.pop_back();
    }
    mDynamicTableSize += itemSize;
    mDynamicHeaderTables.emplace_front(name, value);
}

inline auto HpackContext::findHeaderField(std::string_view name, std::string_view value) -> Result<std::size_t> {
    // TODO: How do you construct a first-in, first-out structure that is easy to search?
    auto index = 0;
    for (const auto &[sname, svalue, stype] : kStaticHeaderTables) {
        ++index;
        if (sname.compare(name) == 0 && (value.empty() || svalue.compare(value) == 0)) {
            return index;
        }
    }
    for (const auto &[dname, dvalue, dtype] : mDynamicHeaderTables) {
        ++index;
        if (dname.compare(name) == 0 && (value.empty() || dvalue.compare(value) == 0)) {
            return index;
        }
    }
    return Unexpected(HpackError::FieldNotInTable);
}

inline HpackDecoder::HpackDecoder(HpackContext &context) : mContext(context) {
}

inline HpackDecoder::HpackDecoder(HpackContext &context, std::span<const std::byte> buffer, const int offset)
    : mContext(context) {
}

inline auto HpackDecoder::decode(std::span<const std::byte> buffer) -> Result<void> {
    ILIAS_ASSERT(buffer.size() >= 1);
    int         offset = 0;
    Result<int> ret    = Unexpected(HpackError::UnknownError);
    while (offset < buffer.size()) {
        if (static_cast<unsigned char>(buffer[offset]) & 0x80) {
            ret = indexedHeaderField(buffer.subspan(offset));
            if (!ret) {
                return Unexpected(ret.error());
            }
            offset += ret.value();
            mDecodeHeaderList.back().type = HeaderFieldType::Indexed;
        }
        else if ((static_cast<unsigned char>(buffer[offset]) >> 6) == 1) {
            ret = literalHeaderField(buffer.subspan(offset), true);
            if (!ret) {
                return Unexpected(ret.error());
            }
            offset += ret.value();
            mDecodeHeaderList.back().type = HeaderFieldType::IncrementalIndexing;
        }
        else if ((static_cast<unsigned char>(buffer[offset]) >> 4) == 0) {
            ret = literalHeaderField(buffer.subspan(offset), false);
            if (!ret) {
                return Unexpected(ret.error());
            }
            offset += ret.value();
            mDecodeHeaderList.back().type = HeaderFieldType::WithoutIndexing;
        }
        else if ((static_cast<unsigned char>(buffer[offset]) >> 4) == 1) {
            ret = literalHeaderField(buffer.subspan(offset), false);
            if (!ret) {
                return Unexpected(ret.error());
            }
            offset += ret.value();
            mDecodeHeaderList.back().type = HeaderFieldType::NeverIndexed;
        }
        else if ((static_cast<unsigned char>(buffer[offset]) >> 5) == 1) {
            ret = updateDynamicTableSize(buffer.subspan(offset));
            if (!ret) {
                return Unexpected(ret.error());
            }
            offset += ret.value();
        }
        else {
            return Unexpected(HpackError::UnknowHeaderField);
        }
    }
    ILIAS_ASSERT(offset == buffer.size());
    return Result<void> {};
}

inline auto HpackDecoder::headerFieldList() const noexcept -> const std::vector<HeaderField> & {
    return mDecodeHeaderList;
}

inline auto HpackDecoder::headerFieldList() noexcept -> std::vector<HeaderField> & {
    return mDecodeHeaderList;
}

inline auto HpackDecoder::clear() noexcept -> void {
    mDecodeHeaderList.clear();
}

inline auto HpackDecoder::indexedHeaderField(std::span<const std::byte> buffer) -> Result<int> {
    int  index  = -1;
    auto offset = decodeInt(buffer, index, 7);
    if (!offset) {
        return Unexpected(HpackError::IndexParserError);
    }
    if (index < 0 || offset.value() < 0) {
        return Unexpected(HpackError::IndexParserError);
    }
    auto ret = mContext.indexToHeaderField(index);
    if (!ret) {
        return Unexpected(ret.error());
    }
    mDecodeHeaderList.push_back(ret.value());
    return offset.value();
}

inline auto HpackDecoder::literalHeaderField(std::span<const std::byte> buffer, const bool incremental) -> Result<int> {
    int  index  = -1;
    auto offset = decodeInt(buffer, index, incremental ? 6 : 4);
    if (!offset || offset.value() < 1) {
        return Unexpected(offset.error_or(HpackError::IndexParserError));
    }
    // It shouldn't have appeared, if decodeInt not returned error and not get value. please fixit.
    ILIAS_ASSERT(index >= 0);
    if (index == 0) { // Literal Header Field with Incremental Indexing -- New Name
        std::string name, value;
        auto        nameOffset = decodeString(buffer.subspan(offset.value()), name);
        if (!nameOffset) {
            return Unexpected(nameOffset.error());
        }
        auto valueOffset = decodeString(buffer.subspan(offset.value() + nameOffset.value()), value);
        if (!valueOffset) {
            return Unexpected(valueOffset.error());
        }
        if (incremental) {
            mContext.appendHeaderField(name, value);
        }
        mDecodeHeaderList.push_back({name, value});
        return offset.value() + nameOffset.value() + valueOffset.value();
    }
    // Literal Header Field with Incremental Indexing -- Indexed Name
    auto header = mContext.indexToHeaderField(index);
    if (!header) {
        return Unexpected(header.error());
    }
    std::string value;
    auto        valueOffset = decodeString(buffer.subspan(offset.value()), value);
    if (!valueOffset) {
        return Unexpected(valueOffset.error());
    }
    if (incremental) {
        mContext.appendHeaderField(header.value().headerName, value);
    }
    mDecodeHeaderList.push_back({header.value().headerName, value});
    return offset.value() + valueOffset.value();
}

inline auto HpackDecoder::updateDynamicTableSize(std::span<const std::byte> buffer) -> Result<int> {
    int64_t size = -1;
    auto    ret  = decodeInt(buffer, size, 5);
    if (!ret) {
        return Unexpected(ret.error());
    }
    auto ret1 = mContext.setMaxDynamicTableSize(size);
    if (!ret1) {
        return Unexpected(ret1.error());
    }
    return ret.value();
}

template <typename T>
inline auto HpackDecoder::decodeInt(std::span<const std::byte> buffer, T &value, const int allowPrefixBits) const
    -> Result<int> {
    ILIAS_ASSERT(buffer.size() > 0);
    ILIAS_ASSERT(allowPrefixBits <= 8);
    auto ret = IntegerDecoder::decode(buffer, value, 8 - allowPrefixBits);
    if (ret < 0) {
        return Unexpected(ret == -1 ? HpackError::IntegerOverflow : HpackError::NeedMoreData);
    }
    return ret;
}

inline auto HpackDecoder::decodeString(std::span<const std::byte> buffer, std::string &value) const -> Result<int> {
    ILIAS_ASSERT(buffer.size() > 0);
    bool        isHuffman = static_cast<unsigned char>(buffer[0]) & 0b10000000U;
    std::size_t length;
    auto        ret = decodeInt(buffer, length, 7);
    if (!ret) {
        return Unexpected(ret.error());
    }
    auto length_length = ret.value();
    if (length_length < 0 || length_length + length > buffer.size()) {
        return Unexpected(HpackError::NeedMoreData);
    }
    if (isHuffman) {
        std::vector<std::byte> decoded;
        auto                   decode_length = HuffmanDecoder::decode(buffer.subspan(length_length, length), decoded);
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

inline HpackEncoder::HpackEncoder(HpackContext &context) : mContext(context) {
    mBuffer.reserve(1024);
}

inline auto HpackEncoder::encode(const std::vector<HeaderField> &headerList, const bool huffman) -> Result<void> {
    for (auto &header : headerList) {
        auto ret = encode(header, huffman);
        if (!ret) {
            return Unexpected(ret.error());
        }
    }
    return Result<void>();
}

inline auto HpackEncoder::encode(HeaderFieldView header, const bool huffman) -> Result<void> {
    Result<std::size_t> index = Unexpected(HpackError::FieldNotInTable);
    while (header.type == HeaderFieldType::Unknow) {
        index = mContext.findHeaderField(header.headerName, header.headerValue);
        if (index) { // has in context table
            header.type = HeaderFieldType::Indexed;
            break;
        }
        header.type = HeaderFieldType::IncrementalIndexing; // not in context table, default insert in table.
    }
    switch (header.type) {
        case HeaderFieldType::Indexed:
            if (!index) {
                auto ret = mContext.findHeaderField(header.headerName, header.headerValue);
                if (!ret) {
                    return Unexpected(ret.error());
                }
                index = ret;
            }
            mBuffer.push_back(static_cast<std::byte>(0x80));
            return indexedHeaderField(index.value());
        case HeaderFieldType::IncrementalIndexing:
            index = mContext.findHeaderField(header.headerName);
            mBuffer.push_back(static_cast<std::byte>(0x40));
            if (!index) {
                return literalHeaderField(header.headerName, header.headerValue, true, huffman);
            }
            else {
                return literalHeaderField(index.value(), header.headerValue, true, huffman);
            }
        case HeaderFieldType::WithoutIndexing:
            index = mContext.findHeaderField(header.headerName);
            mBuffer.push_back(static_cast<std::byte>(0x00));
            if (!index) {
                return literalHeaderField(header.headerName, header.headerValue, false, huffman);
            }
            else {
                return literalHeaderField(index.value(), header.headerValue, false, huffman);
            }
        case HeaderFieldType::NeverIndexed:
            mBuffer.push_back(static_cast<std::byte>(0x01));
            return literalHeaderField(header.headerName, header.headerValue, false, huffman);
        default:
            return Unexpected(HpackError::UnknowFieldType);
    }
}

inline auto HpackEncoder::encode(std::string_view name, std::string_view value, const HeaderFieldType type,
                                 const bool huffman) -> Result<void> {
    return encode(HeaderFieldView(name, value, type), huffman);
}

inline auto HpackEncoder::clear() -> void {
    mBuffer.clear();
}

template <typename T>
inline auto HpackEncoder::encodeInt(T &&value, const int allowPrefixBits) -> Result<void> {
    ILIAS_ASSERT(allowPrefixBits <= 8);
    auto ret = IntegerEncoder::encode(std::forward<T>(value), mBuffer, 8 - allowPrefixBits);
    if (ret < 0) {
        return Unexpected(HpackError::UnknownError);
    }
    return Result<void> {};
}

inline auto HpackEncoder::encodeString(std::string_view value, const bool huffmanEncoding) -> Result<void> {
    if (huffmanEncoding) {
        std::vector<std::byte> buffer;

        if (HuffmanEncoder::encode({reinterpret_cast<const std::byte *>(value.data()), value.size()}, buffer) != 0) {
            return Unexpected(HpackError::UnknownError);
        }
        mBuffer.push_back(static_cast<std::byte>(0x80));
        auto ret = encodeInt(buffer.size(), 7);
        if (!ret) {
            return Unexpected(ret.error());
        }
        mBuffer.insert(mBuffer.end(), buffer.begin(), buffer.end());
        return Result<void> {};
    }
    else {
        mBuffer.push_back(static_cast<std::byte>(0x00));
        auto ret = encodeInt(value.size(), 7);
        if (!ret) {
            return Unexpected(ret.error());
        }
        mBuffer.insert(mBuffer.end(), reinterpret_cast<const std::byte *>(value.data()),
                       reinterpret_cast<const std::byte *>(value.data()) + value.size());
        return Result<void> {};
    }
}

inline auto HpackEncoder::buffer() -> std::vector<std::byte> & {
    return mBuffer;
}
inline auto HpackEncoder::buffer() const -> const std::vector<std::byte> & {
    return mBuffer;
}
inline auto HpackEncoder::size() const -> std::size_t {
    return mBuffer.size();
}

inline auto HpackEncoder::literalHeaderField(std::string_view name, std::string_view value, const bool incremental,
                                             const bool huffman) -> Result<void> {
    if (incremental) {
        mContext.appendHeaderField(name, value);
    }
    auto ret = encodeString(name, huffman);
    if (!ret) {
        return Unexpected(ret.error());
    }
    ret = encodeString(value, huffman);
    return ret;
}

inline auto HpackEncoder::literalHeaderField(std::size_t name_index, std::string_view value, const bool incremental,
                                             const bool huffman) -> Result<void> {
    ILIAS_ASSERT(name_index < mContext.dynamicTableIndexSize() + mContext.staticTableIndexSize());
    if (incremental) {
        auto ret = mContext.indexToHeaderField(name_index);
        if (!ret) {
            return Unexpected(ret.error());
        }
        mContext.appendHeaderField(ret.value().headerName, value);
        auto ret1 = encodeInt(name_index, 6);
        if (!ret1) {
            return Unexpected(ret1.error());
        }
        return encodeString(value, huffman);
    }
    else {
        auto ret = encodeInt(name_index, 4);
        if (!ret) {
            return Unexpected(ret.error());
        }
        return encodeString(value, huffman);
    }
}

inline auto HpackEncoder::indexedHeaderField(const std::size_t index) -> Result<void> {
    ILIAS_ASSERT(index <= mContext.dynamicTableIndexSize() + mContext.staticTableIndexSize());
    ILIAS_ASSERT(index > 0);
    return encodeInt(index, 7);
}

inline auto HpackEncoder::updateDynamicTableSize(const std::size_t size) -> Result<void> {
    if (size > mContext.limitDynamicTableSize()) {
        return Unexpected(HpackError::SizeOutOfLimit);
    }
    auto ret = mContext.setMaxDynamicTableSize(size);
    if (!ret) {
        return Unexpected(ret.error());
    }
    mBuffer.push_back(std::byte {0x20});
    return encodeInt(size, 5);
}

inline auto HpackErrorCategory::message(int64_t value) const -> std::string {
    return std::to_string(value);
}
inline auto HpackErrorCategory::name() const -> std::string_view {
    return "hpack_error";
}
inline auto HpackErrorCategory::equivalent(int64_t self, const Error &other) const -> bool {
    return other.category().name() == name() && other.value() == self;
}
inline auto HpackErrorCategory::instance() -> const HpackErrorCategory & {
    static HpackErrorCategory instance;
    return instance;
}

} // namespace http2

ILIAS_NS_END