#include <gtest/gtest.h>

#include <ilias/http/detail/hpack.hpp>
#include <ilias/http/detail/dictionary_tree.hpp>

using namespace ILIAS_NAMESPACE::http2;
using namespace ILIAS_NAMESPACE::http2::detail;

TEST(Hpack, ContextTestStaticTable) {
    HpackContext context;
    {
        auto field = context.indexToHeaderField(1).value();
        EXPECT_STREQ(field.headerName.data(), ":authority");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(2).value();
        EXPECT_STREQ(field.headerName.data(), ":method");
        EXPECT_STREQ(field.headerValue.data(), "GET");
    }
    {
        auto field = context.indexToHeaderField(3).value();
        EXPECT_STREQ(field.headerName.data(), ":method");
        EXPECT_STREQ(field.headerValue.data(), "POST");
    }
    {
        auto field = context.indexToHeaderField(4).value();
        EXPECT_STREQ(field.headerName.data(), ":path");
        EXPECT_STREQ(field.headerValue.data(), "/");
    }
    {
        auto field = context.indexToHeaderField(5).value();
        EXPECT_STREQ(field.headerName.data(), ":path");
        EXPECT_STREQ(field.headerValue.data(), "/index.html");
    }
    {
        auto field = context.indexToHeaderField(6).value();
        EXPECT_STREQ(field.headerName.data(), ":scheme");
        EXPECT_STREQ(field.headerValue.data(), "http");
    }
    {
        auto field = context.indexToHeaderField(7).value();
        EXPECT_STREQ(field.headerName.data(), ":scheme");
        EXPECT_STREQ(field.headerValue.data(), "https");
    }
    {
        auto field = context.indexToHeaderField(8).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "200");
    }
    {
        auto field = context.indexToHeaderField(9).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "204");
    }
    {
        auto field = context.indexToHeaderField(10).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "206");
    }
    {
        auto field = context.indexToHeaderField(11).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "304");
    }
    {
        auto field = context.indexToHeaderField(12).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "400");
    }
    {
        auto field = context.indexToHeaderField(13).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "404");
    }
    {
        auto field = context.indexToHeaderField(14).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "500");
    }
    {
        auto field = context.indexToHeaderField(15).value();
        EXPECT_STREQ(field.headerName.data(), "accept-charset");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(16).value();
        EXPECT_STREQ(field.headerName.data(), "accept-encoding");
        EXPECT_STREQ(field.headerValue.data(), "gzip, deflate");
    }
    {
        auto field = context.indexToHeaderField(17).value();
        EXPECT_STREQ(field.headerName.data(), "accept-language");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(18).value();
        EXPECT_STREQ(field.headerName.data(), "accept-ranges");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(19).value();
        EXPECT_STREQ(field.headerName.data(), "accept");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(20).value();
        EXPECT_STREQ(field.headerName.data(), "access-control-allow-origin");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(21).value();
        EXPECT_STREQ(field.headerName.data(), "age");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(22).value();
        EXPECT_STREQ(field.headerName.data(), "allow");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(23).value();
        EXPECT_STREQ(field.headerName.data(), "authorization");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(24).value();
        EXPECT_STREQ(field.headerName.data(), "cache-control");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(25).value();
        EXPECT_STREQ(field.headerName.data(), "content-disposition");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(26).value();
        EXPECT_STREQ(field.headerName.data(), "content-encoding");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(27).value();
        EXPECT_STREQ(field.headerName.data(), "content-language");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(28).value();
        EXPECT_STREQ(field.headerName.data(), "content-length");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(29).value();
        EXPECT_STREQ(field.headerName.data(), "content-location");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(30).value();
        EXPECT_STREQ(field.headerName.data(), "content-range");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(31).value();
        EXPECT_STREQ(field.headerName.data(), "content-type");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(32).value();
        EXPECT_STREQ(field.headerName.data(), "cookie");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(33).value();
        EXPECT_STREQ(field.headerName.data(), "date");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(34).value();
        EXPECT_STREQ(field.headerName.data(), "etag");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(35).value();
        EXPECT_STREQ(field.headerName.data(), "expect");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(36).value();
        EXPECT_STREQ(field.headerName.data(), "expires");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(37).value();
        EXPECT_STREQ(field.headerName.data(), "from");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(38).value();
        EXPECT_STREQ(field.headerName.data(), "host");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(39).value();
        EXPECT_STREQ(field.headerName.data(), "if-match");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(40).value();
        EXPECT_STREQ(field.headerName.data(), "if-modified-since");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(41).value();
        EXPECT_STREQ(field.headerName.data(), "if-none-match");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(42).value();
        EXPECT_STREQ(field.headerName.data(), "if-range");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(43).value();
        EXPECT_STREQ(field.headerName.data(), "if-unmodified-since");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(44).value();
        EXPECT_STREQ(field.headerName.data(), "last-modified");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(45).value();
        EXPECT_STREQ(field.headerName.data(), "link");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(46).value();
        EXPECT_STREQ(field.headerName.data(), "location");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(47).value();
        EXPECT_STREQ(field.headerName.data(), "max-forwards");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(48).value();
        EXPECT_STREQ(field.headerName.data(), "proxy-authenticate");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(49).value();
        EXPECT_STREQ(field.headerName.data(), "proxy-authorization");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(50).value();
        EXPECT_STREQ(field.headerName.data(), "range");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(51).value();
        EXPECT_STREQ(field.headerName.data(), "referer");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(52).value();
        EXPECT_STREQ(field.headerName.data(), "refresh");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(53).value();
        EXPECT_STREQ(field.headerName.data(), "retry-after");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(54).value();
        EXPECT_STREQ(field.headerName.data(), "server");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(55).value();
        EXPECT_STREQ(field.headerName.data(), "set-cookie");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(56).value();
        EXPECT_STREQ(field.headerName.data(), "strict-transport-security");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(57).value();
        EXPECT_STREQ(field.headerName.data(), "transfer-encoding");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(58).value();
        EXPECT_STREQ(field.headerName.data(), "user-agent");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(59).value();
        EXPECT_STREQ(field.headerName.data(), "vary");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(60).value();
        EXPECT_STREQ(field.headerName.data(), "via");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToHeaderField(61).value();
        EXPECT_STREQ(field.headerName.data(), "www-authenticate");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
}

TEST(Hpack, ContextDynamicTable) {
    HpackContext context;
    context.appendHeaderField("custom-header1", "custom-value");
    context.appendHeaderField("custom-header1", "custom-value1");
    context.appendHeaderField("custom-header3", "custom-value3");
    EXPECT_EQ(context.dynamicTableSize(), 176);

    auto field = context.indexToHeaderField(62).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header3");
    EXPECT_STREQ(field.headerValue.data(), "custom-value3");
    field = context.indexToHeaderField(63).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header1");
    EXPECT_STREQ(field.headerValue.data(), "custom-value1");

    context.setMaxDynamicTableSize(70);
    EXPECT_EQ(context.dynamicTableSize(), 59);
    field = context.indexToHeaderField(62).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header3");
    EXPECT_STREQ(field.headerValue.data(), "custom-value3");

    context.appendHeaderField(
        "custom-header1", "a very very big value for this test,                                                     "
                          "                                                                                         ");
    auto ret = context.indexToHeaderField(62);
    EXPECT_EQ(ret.error(), HpackError::IndexOutOfRange);

    context.appendHeaderField("custom-header1", "custom-value1");
    EXPECT_EQ(context.dynamicTableSize(), 59);
    field = context.indexToHeaderField(62).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header1");
    EXPECT_STREQ(field.headerValue.data(), "custom-value1");
    context.setMaxDynamicTableSize(0);
    EXPECT_EQ(context.dynamicTableSize(), 0);
}

TEST(Hpack, HuffmanCodeTest) {
    std::byte input[10] = {std::byte {'a'}, std::byte {'b'}, std::byte {'c'}, std::byte {'d'}, std::byte {'e'}};

    std::vector<std::byte> outputBuffer;
    detail::HuffmanEncoder encoder;
    encoder.encode({input, 5}, outputBuffer);
    // |00011 |100011 |00100 |100100 |00101
    // 0001|1100|0110|0100|1001|0000|1011|
    // 0x1c 0x64 0x90 0xbf
    std::vector<std::byte> ansower = {std::byte {0x1c}, std::byte {0x64}, std::byte {0x90}, std::byte {0xbf}};
    ASSERT_EQ(outputBuffer.size(), ansower.size());
    for (int i = 0; i < outputBuffer.size(); i++) {
        EXPECT_EQ(outputBuffer[i], ansower[i]);
    }

    std::vector<std::byte> outputBuffer2;
    detail::HuffmanDecoder decoder;
    decoder.decode(outputBuffer, outputBuffer2);
    ASSERT_EQ(outputBuffer2.size(), 5);
    EXPECT_EQ(memcmp(outputBuffer2.data(), input, 5), 0);
}

namespace ILIAS_NAMESPACE::http2 {
class HpackDecoderTest {
public:
    HpackDecoderTest() : context(), decoder(context) {}
    auto decode(std::span<const std::byte> buffer) -> ILIAS_NAMESPACE::Result<void> { return decoder.decode(buffer); }
    auto headerFieldList() const noexcept -> const std::vector<HeaderField> & { return decoder.headerFieldList(); }
    auto headerFieldList() noexcept -> std::vector<HeaderField> & { return decoder.headerFieldList(); }
    auto clear() noexcept -> void { decoder.clear(); }
    auto indexedHeaderField(std::span<const std::byte> buffer) -> ILIAS_NAMESPACE::Result<int> {
        return decoder.indexedHeaderField(buffer);
    }
    auto literalHeaderField(std::span<const std::byte> buffer, const bool incremental = true)
        -> ILIAS_NAMESPACE::Result<int> {
        return decoder.literalHeaderField(buffer, incremental);
    }
    auto updateDynamicTableSize(std::span<const std::byte> buffer) -> ILIAS_NAMESPACE::Result<int> {
        return decoder.updateDynamicTableSize(buffer);
    }
    template <typename T>
    auto getInt(std::span<const std::byte> buffer, T &value, const int allowPrefixBits = 8) const
        -> ILIAS_NAMESPACE::Result<int> {
        return decoder.decodeInt(buffer, value, allowPrefixBits);
    }
    auto getString(std::span<const std::byte> buffer, std::string &value) const -> ILIAS_NAMESPACE::Result<int> {
        return decoder.decodeString(buffer, value);
    }
    HpackContext context;
    HpackDecoder decoder;
};
} // namespace ILIAS_NAMESPACE::http2

TEST(Hpack, IntDecoderTest) {
    HpackDecoderTest decoder;
    std::byte        buffer[1];
    buffer[0] = std::byte {0xf2};
    int value;
    EXPECT_EQ(decoder.getInt(buffer, value).value(), 1);
    EXPECT_EQ(value, 242);

    std::vector<std::byte> buffer4;
    EXPECT_EQ(IntegerEncoder::encode(242, buffer4), 0);
    EXPECT_EQ(buffer4.size(), 1);
    EXPECT_EQ(buffer4[0], std::byte {0xf2});

    std::byte buffer2[5] = {std::byte {0xff}, std::byte {0xf2}, std::byte {0x83}, std::byte {0xf4}, std::byte {0x7f}};
    EXPECT_EQ(decoder.getInt(buffer2, value).value(), 5);
    EXPECT_EQ(value, 268239601);

    buffer4.clear();
    EXPECT_EQ(IntegerEncoder::encode(268239601, buffer4), 0);
    EXPECT_EQ(buffer4.size(), 5);
    EXPECT_EQ(memcmp(buffer4.data(), buffer2, 5), 0);

    std::byte buffer3[6] = {std::byte {0xff}, std::byte {0xf2}, std::byte {0x83},
                            std::byte {0xf4}, std::byte {0x8f}, std::byte {0x70}};
    EXPECT_EQ(decoder.getInt(buffer3, value).error(), HpackError::IntegerOverflow);

    buffer[0] = std::byte {10};
    EXPECT_EQ(decoder.getInt(buffer, value, 5).value(), 1);
    EXPECT_EQ(value, 10);

    buffer4.clear();
    EXPECT_EQ(IntegerEncoder::encode(10, buffer4, 3), 0);
    EXPECT_EQ(buffer4.size(), 1);
    EXPECT_EQ(buffer4[0], std::byte {10});

    buffer2[0] = std::byte {31};
    buffer2[1] = std::byte {0b10011010};
    buffer2[2] = std::byte {0b00001010};
    EXPECT_EQ(decoder.getInt({buffer2, 3}, value, 5).value(), 3);
    EXPECT_EQ(value, 1337);

    buffer4.clear();
    EXPECT_EQ(IntegerEncoder::encode(1337, buffer4, 3), 0);
    EXPECT_EQ(buffer4.size(), 3);
    EXPECT_EQ(memcmp(buffer4.data(), buffer2, 3), 0);
}
namespace ILIAS_NAMESPACE::http2 {
class HpackEncoderTest {
public:
    HpackEncoderTest() : context(), encoder(context) {}

    auto encode(const std::vector<HeaderField> &headerList) -> ILIAS_NAMESPACE::Result<void> {
        return encoder.encode(headerList);
    }
    auto encode(HeaderFieldView header) -> ILIAS_NAMESPACE::Result<void> { return encoder.encode(header); }
    auto encode(std::string_view name, std::string_view value, const HeaderFieldType type = HeaderFieldType::Unknow)
        -> ILIAS_NAMESPACE::Result<void> {
        return encoder.encode(name, value, type);
    }
    auto literalHeaderField(std::string_view name, std::string_view value, const bool incremental = true,
                            const bool huffman = true) -> Result<void> {
        return encoder.literalHeaderField(name, value, incremental, huffman);
    }
    auto literalHeaderField(std::size_t name_index, std::string_view value, const bool incremental = true,
                            const bool huffman = true) -> Result<void> {
        return encoder.literalHeaderField(name_index, value, incremental, huffman);
    }
    auto indexedHeaderField(const std::size_t index) -> Result<void> { return encoder.indexedHeaderField(index); }
    auto updateDynamicTableSize(const std::size_t index) -> Result<void> {
        return encoder.updateDynamicTableSize(index);
    }
    auto clear() -> void { encoder.clear(); }
    auto buffer() -> std::vector<std::byte> & { return encoder.buffer(); }
    auto buffer() const -> const std::vector<std::byte> & { return encoder.buffer(); }
    auto size() const -> std::size_t { return encoder.size(); }
    template <typename T>
    auto saveInt(T &&value, const int allowPrefixBits = 8) -> ILIAS_NAMESPACE::Result<void> {
        return encoder.encodeInt(std::forward<T>(value), allowPrefixBits);
    }
    auto saveString(const std::string &value, const bool huffmanEncoding = false) -> ILIAS_NAMESPACE::Result<void> {
        return encoder.encodeString(value, huffmanEncoding);
    }
    HpackContext context;
    HpackEncoder encoder;
};
} // namespace ILIAS_NAMESPACE::http2

TEST(Hpack, EncoderDecoder) {
    HpackEncoderTest encoder;

    std::string str_data      = "Hello, World!";
    std::byte   encode_data[] = {std::byte {0x0D}, std::byte {'H'}, std::byte {'e'}, std::byte {'l'}, std::byte {'l'},
                                 std::byte {'o'},  std::byte {','}, std::byte {' '}, std::byte {'W'}, std::byte {'o'},
                                 std::byte {'r'},  std::byte {'l'}, std::byte {'d'}, std::byte {'!'}};
    EXPECT_TRUE(encoder.saveString(str_data).has_value());
    EXPECT_EQ(encoder.buffer().size(), 14);
    for (size_t i = 0; i < 14; ++i) {
        EXPECT_EQ(encoder.buffer()[i], encode_data[i]);
    }

    HpackDecoderTest decoder;
    std::string      str_data2;
    EXPECT_EQ(decoder.getString(encode_data, str_data2).value(), 14);
    EXPECT_STREQ(str_data2.c_str(), str_data.c_str());

    encoder.clear();
    // 1100|0110|0101|1010|0010|1000|0011|1111|1101|0010|1001|1100|1000|1111|0110|0101|0001|0010|0111|1111|0001|1111
    // C6|5A|28|3F|D2|9C|8F|65|12|7F|1F
    std::byte encode_data2[] = {std::byte {0x8B}, std::byte {0xC6}, std::byte {0x5A}, std::byte {0x28},
                                std::byte {0x3F}, std::byte {0xD2}, std::byte {0x9C}, std::byte {0x8F},
                                std::byte {0x65}, std::byte {0x12}, std::byte {0x7F}, std::byte {0x1F}};
    EXPECT_TRUE(encoder.saveString(str_data, true).has_value());
    EXPECT_EQ(encoder.buffer().size(), 12);
    for (size_t i = 0; i < 12; ++i) {
        EXPECT_EQ(encoder.buffer()[i], encode_data2[i]);
    }

    EXPECT_EQ(decoder.getString(encode_data2, str_data2).value(), 12);
    EXPECT_STREQ(str_data2.c_str(), str_data.c_str());
};

TEST(Hpack, LiteralHeaderFieldWithIndexing) {
    HpackContext context;
    HpackDecoder decoder(context);

    std::byte data[] = {
        std::byte {0x40}, std::byte {0x0a}, std::byte {0x63}, std::byte {0x75}, std::byte {0x73}, std::byte {0x74},
        std::byte {0x6f}, std::byte {0x6d}, std::byte {0x2d}, std::byte {0x6b}, std::byte {0x65}, std::byte {0x79},
        std::byte {0x0d}, std::byte {0x63}, std::byte {0x75}, std::byte {0x73}, std::byte {0x74}, std::byte {0x6f},
        std::byte {0x6d}, std::byte {0x2d}, std::byte {0x68}, std::byte {0x65}, std::byte {0x61}, std::byte {0x64},
        std::byte {0x65}, std::byte {0x72},
    };

    EXPECT_TRUE(decoder.decode(data).operator bool());
    EXPECT_EQ(decoder.headerFieldList().size(), 1);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), "custom-key");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "custom-header");
    EXPECT_EQ(decoder.headerFieldList()[0].type, HeaderFieldType::IncrementalIndexing);
    EXPECT_EQ(context.dynamicTableIndexSize(), 1);
    auto filed = context.indexToHeaderField(62);
    ASSERT_TRUE(filed.has_value());
    EXPECT_STREQ(filed.value().headerName.data(), "custom-key");
    EXPECT_STREQ(filed.value().headerValue.data(), "custom-header");
}

TEST(Hpack, LiteralHeaderFieldWithoutIndexing) {
    HpackContext context;
    HpackDecoder decoder(context);

    std::byte data[] = {std::byte {0x04}, std::byte {0x0c}, std::byte {0x2f}, std::byte {0x73}, std::byte {0x61},
                        std::byte {0x6d}, std::byte {0x70}, std::byte {0x6c}, std::byte {0x65}, std::byte {0x2f},
                        std::byte {0x70}, std::byte {0x61}, std::byte {0x74}, std::byte {0x68}};
    EXPECT_TRUE(decoder.decode(data).operator bool());
    EXPECT_EQ(decoder.headerFieldList().size(), 1);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":path");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "/sample/path");
    EXPECT_EQ(context.dynamicTableIndexSize(), 0);
}

TEST(Hpack, LiteralHeaderFieldNeverIndexed) {
    HpackContext context;
    HpackDecoder decoder(context);

    std::byte data[] = {std::byte {0x10}, std::byte {0x08}, std::byte {0x70}, std::byte {0x61}, std::byte {0x73},
                        std::byte {0x73}, std::byte {0x77}, std::byte {0x6f}, std::byte {0x72}, std::byte {0x64},
                        std::byte {0x06}, std::byte {0x73}, std::byte {0x65}, std::byte {0x63}, std::byte {0x72},
                        std::byte {0x65}, std::byte {0x74}};
    EXPECT_TRUE(decoder.decode(data).operator bool());
    EXPECT_EQ(decoder.headerFieldList().size(), 1);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), "password");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "secret");
    EXPECT_EQ(context.dynamicTableIndexSize(), 0);
}

TEST(Hpack, IndexedHeaderField) {
    HpackContext context;
    HpackDecoder decoder(context);

    std::byte data[] = {std::byte {0x82}};
    EXPECT_TRUE(decoder.decode(data).operator bool());
    EXPECT_EQ(decoder.headerFieldList().size(), 1);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":method");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "GET");
    EXPECT_EQ(context.dynamicTableIndexSize(), 0);
}

TEST(Hpack, Request) {
    HpackContext context;
    HpackDecoder decoder(context);

    HpackContext context1;
    HpackEncoder encoder(context1);
    /* first request
        :method: GET
        :scheme: http
        :path: /
        :authority: www.example.com
    */
    std::byte request_data1[] = {
        std::byte {0x82}, std::byte {0x86}, std::byte {0x84}, std::byte {0x41}, std::byte {0x0f},
        std::byte {0x77}, std::byte {0x77}, std::byte {0x77}, std::byte {0x2e}, std::byte {0x65},
        std::byte {0x78}, std::byte {0x61}, std::byte {0x6d}, std::byte {0x70}, std::byte {0x6c},
        std::byte {0x65}, std::byte {0x2e}, std::byte {0x63}, std::byte {0x6f}, std::byte {0x6d},
    };
    EXPECT_TRUE(decoder.decode(request_data1).operator bool());

    EXPECT_EQ(decoder.headerFieldList().size(), 4);
    EXPECT_EQ(context.dynamicTableSize(), 57);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":method");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "GET");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), ":scheme");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "http");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), ":path");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "/");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), ":authority");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "www.example.com");

    EXPECT_TRUE(encoder.encode(decoder.headerFieldList(), false).operator bool());
    ASSERT_EQ(encoder.buffer().size(), sizeof(request_data1));
    for (int i = 0; i < sizeof(request_data1); ++i) {
        EXPECT_EQ(encoder.buffer()[i], request_data1[i]) << "at index " << i;
    }

    /* Second request
        :method: GET
        :scheme: http
        :path: /
        :authority: www.example.com
        cache-control: no-cache
    */
    std::byte request_data2[] = {std::byte {0x82}, std::byte {0x86}, std::byte {0x84}, std::byte {0xbe},
                                 std::byte {0x58}, std::byte {0x08}, std::byte {0x6e}, std::byte {0x6f},
                                 std::byte {0x2d}, std::byte {0x63}, std::byte {0x61}, std::byte {0x63},
                                 std::byte {0x68}, std::byte {0x65}};
    decoder.clear();
    EXPECT_TRUE(decoder.decode(request_data2).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 110);
    EXPECT_EQ(decoder.headerFieldList().size(), 5);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":method");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "GET");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), ":scheme");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "http");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), ":path");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "/");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), ":authority");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "www.example.com");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerValue.c_str(), "no-cache");

    encoder.clear();
    auto ret = encoder.encode(decoder.headerFieldList(), false);
    EXPECT_TRUE(ret.operator bool()) << "error: " << ret.error().toString();
    ASSERT_EQ(encoder.buffer().size(), sizeof(request_data2));
    for (int i = 0; i < sizeof(request_data2); ++i) {
        EXPECT_EQ(encoder.buffer()[i], request_data2[i]) << "at index " << i;
    }

    /* Third Request
        :method: GET
        :scheme: https
        :path: /index.html
        :authority: www.example.com
        custom-key: custom-value
    */
    std::byte request_data3[] = {
        std::byte {0x82}, std::byte {0x87}, std::byte {0x85}, std::byte {0xbf}, std::byte {0x40}, std::byte {0x0a},
        std::byte {0x63}, std::byte {0x75}, std::byte {0x73}, std::byte {0x74}, std::byte {0x6f}, std::byte {0x6d},
        std::byte {0x2d}, std::byte {0x6b}, std::byte {0x65}, std::byte {0x79}, std::byte {0x0c}, std::byte {0x63},
        std::byte {0x75}, std::byte {0x73}, std::byte {0x74}, std::byte {0x6f}, std::byte {0x6d}, std::byte {0x2d},
        std::byte {0x76}, std::byte {0x61}, std::byte {0x6c}, std::byte {0x75}, std::byte {0x65},
    };
    decoder.clear();
    EXPECT_TRUE(decoder.decode(request_data3).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 164);
    EXPECT_EQ(decoder.headerFieldList().size(), 5);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":method");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "GET");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), ":scheme");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "https");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), ":path");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "/index.html");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), ":authority");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "www.example.com");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerName.c_str(), "custom-key");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerValue.c_str(), "custom-value");

    encoder.clear();
    EXPECT_TRUE(encoder.encode(decoder.headerFieldList(), false).operator bool());
    ASSERT_EQ(encoder.buffer().size(), sizeof(request_data3));
    for (int i = 0; i < sizeof(request_data3); ++i) {
        EXPECT_EQ(encoder.buffer()[i], request_data3[i]) << "at index " << i;
    }
}

TEST(Hpack, RequestWithHuffmanConding) {
    HpackContext context;
    HpackDecoder decoder(context);

    HpackContext context1;
    HpackEncoder encoder(context1);

    /* First Request
        :method: GET
        :scheme: http
        :path: /
        :authority: www.example.com
    */
    std::byte request_data1[] = {
        std::byte {0x82}, std::byte {0x86}, std::byte {0x84}, std::byte {0x41}, std::byte {0x8c}, std::byte {0xf1},
        std::byte {0xe3}, std::byte {0xc2}, std::byte {0xe5}, std::byte {0xf2}, std::byte {0x3a}, std::byte {0x6b},
        std::byte {0xa0}, std::byte {0xab}, std::byte {0x90}, std::byte {0xf4}, std::byte {0xff},
    };
    EXPECT_TRUE(decoder.decode(request_data1).operator bool());

    EXPECT_EQ(context.dynamicTableSize(), 57);
    EXPECT_EQ(decoder.headerFieldList().size(), 4);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":method");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "GET");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), ":scheme");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "http");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), ":path");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "/");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), ":authority");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "www.example.com");

    EXPECT_TRUE(encoder.encode(decoder.headerFieldList()).operator bool());
    ASSERT_EQ(encoder.buffer().size(), sizeof(request_data1));
    for (int i = 0; i < sizeof(request_data1); ++i) {
        EXPECT_EQ(encoder.buffer()[i], request_data1[i]) << "at index " << i;
    }
    /* Second request
    :method: GET
    :scheme: http
    :path: /
    :authority: www.example.com
    cache-control: no-cache
*/
    std::byte request_data2[] = {
        std::byte {0x82}, std::byte {0x86}, std::byte {0x84}, std::byte {0xbe}, std::byte {0x58}, std::byte {0x86},
        std::byte {0xa8}, std::byte {0xeb}, std::byte {0x10}, std::byte {0x64}, std::byte {0x9c}, std::byte {0xbf},
    };
    decoder.clear();
    EXPECT_TRUE(decoder.decode(request_data2).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 110);
    EXPECT_EQ(decoder.headerFieldList().size(), 5);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":method");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "GET");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), ":scheme");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "http");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), ":path");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "/");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), ":authority");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "www.example.com");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerValue.c_str(), "no-cache");

    encoder.clear();
    EXPECT_TRUE(encoder.encode(decoder.headerFieldList()).operator bool());
    ASSERT_EQ(encoder.buffer().size(), sizeof(request_data2));
    for (int i = 0; i < sizeof(request_data2); ++i) {
        EXPECT_EQ(encoder.buffer()[i], request_data2[i]) << "at index " << i;
    }

    /* Third Request
        :method: GET
        :scheme: https
        :path: /index.html
        :authority: www.example.com
        custom-key: custom-value
    */
    std::byte request_data3[] = {
        std::byte {0x82}, std::byte {0x87}, std::byte {0x85}, std::byte {0xbf}, std::byte {0x40}, std::byte {0x88},
        std::byte {0x25}, std::byte {0xa8}, std::byte {0x49}, std::byte {0xe9}, std::byte {0x5b}, std::byte {0xa9},
        std::byte {0x7d}, std::byte {0x7f}, std::byte {0x89}, std::byte {0x25}, std::byte {0xa8}, std::byte {0x49},
        std::byte {0xe9}, std::byte {0x5b}, std::byte {0xb8}, std::byte {0xe8}, std::byte {0xb4}, std::byte {0xbf},
    };
    decoder.clear();
    EXPECT_TRUE(decoder.decode(request_data3).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 164);
    EXPECT_EQ(decoder.headerFieldList().size(), 5);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":method");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "GET");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), ":scheme");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "https");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), ":path");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "/index.html");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), ":authority");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "www.example.com");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerName.c_str(), "custom-key");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerValue.c_str(), "custom-value");

    encoder.clear();
    EXPECT_TRUE(encoder.encode(decoder.headerFieldList()).operator bool());
    ASSERT_EQ(encoder.buffer().size(), sizeof(request_data3));
    for (int i = 0; i < sizeof(request_data3); ++i) {
        EXPECT_EQ(encoder.buffer()[i], request_data3[i]) << "at index " << i;
    }
}

TEST(Hpack, ResponseWithoutHuffmanConding) {
    HpackContext context;
    context.setLimitDynamicTableSize(256);
    HpackDecoder decoder(context);

    HpackContext context1;
    context1.setLimitDynamicTableSize(256);
    HpackEncoder encoder(context1);

    /*First Response
        :status: 302
        cache-control: private
        date: Mon, 21 Oct 2013 20:13:21 GMT
        location: https://www.example.com
    */
    std::byte response_data1[] = {
        std::byte {0x48}, std::byte {0x03}, std::byte {0x33}, std::byte {0x30}, std::byte {0x32}, std::byte {0x58},
        std::byte {0x07}, std::byte {0x70}, std::byte {0x72}, std::byte {0x69}, std::byte {0x76}, std::byte {0x61},
        std::byte {0x74}, std::byte {0x65}, std::byte {0x61}, std::byte {0x1d}, std::byte {0x4d}, std::byte {0x6f},
        std::byte {0x6e}, std::byte {0x2c}, std::byte {0x20}, std::byte {0x32}, std::byte {0x31}, std::byte {0x20},
        std::byte {0x4f}, std::byte {0x63}, std::byte {0x74}, std::byte {0x20}, std::byte {0x32}, std::byte {0x30},
        std::byte {0x31}, std::byte {0x33}, std::byte {0x20}, std::byte {0x32}, std::byte {0x30}, std::byte {0x3a},
        std::byte {0x31}, std::byte {0x33}, std::byte {0x3a}, std::byte {0x32}, std::byte {0x31}, std::byte {0x20},
        std::byte {0x47}, std::byte {0x4d}, std::byte {0x54}, std::byte {0x6e}, std::byte {0x17}, std::byte {0x68},
        std::byte {0x74}, std::byte {0x74}, std::byte {0x70}, std::byte {0x73}, std::byte {0x3a}, std::byte {0x2f},
        std::byte {0x2f}, std::byte {0x77}, std::byte {0x77}, std::byte {0x77}, std::byte {0x2e}, std::byte {0x65},
        std::byte {0x78}, std::byte {0x61}, std::byte {0x6d}, std::byte {0x70}, std::byte {0x6c}, std::byte {0x65},
        std::byte {0x2e}, std::byte {0x63}, std::byte {0x6f}, std::byte {0x6d},
    };

    EXPECT_TRUE(decoder.decode(response_data1).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 222);
    EXPECT_EQ(decoder.headerFieldList().size(), 4);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":status");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "302");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "private");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), "date");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "Mon, 21 Oct 2013 20:13:21 GMT");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), "location");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "https://www.example.com");

    EXPECT_TRUE(encoder.encode(decoder.headerFieldList(), false).operator bool());
    EXPECT_EQ(encoder.buffer().size(), sizeof(response_data1));
    for (int i = 0; i < std::min(encoder.buffer().size(), sizeof(response_data1)); ++i) {
        EXPECT_EQ(encoder.buffer()[i], response_data1[i]) << "at index " << i;
    }

    /* Second response
        :status: 307
        cache-control: private
        date: Mon, 21 Oct 2013 20:13:21 GMT
        location: https://www.example.com
     */
    std::byte response_data2[] = {
        std::byte {0x48}, std::byte {0x03}, std::byte {0x33}, std::byte {0x30},
        std::byte {0x37}, std::byte {0xc1}, std::byte {0xc0}, std::byte {0xbf},
    };
    decoder.clear();
    EXPECT_TRUE(decoder.decode(response_data2).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 222);
    EXPECT_EQ(decoder.headerFieldList().size(), 4);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":status");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "307");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "private");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), "date");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "Mon, 21 Oct 2013 20:13:21 GMT");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), "location");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "https://www.example.com");

    encoder.clear();
    EXPECT_TRUE(encoder.encode(decoder.headerFieldList(), false).operator bool());
    ASSERT_EQ(encoder.buffer().size(), sizeof(response_data2));
    for (int i = 0; i < sizeof(response_data2); ++i) {
        EXPECT_EQ(encoder.buffer()[i], response_data2[i]) << "at index " << i;
    }

    /* Third Response
        :status: 200
        cache-control: private
        date: Mon, 21 Oct 2013 20:13:22 GMT
        location: https://www.example.com
        content-encoding: gzip
        set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1
    */
    std::byte response_data3[] = {
        std::byte {0x88}, std::byte {0xc1}, std::byte {0x61}, std::byte {0x1d}, std::byte {0x4d}, std::byte {0x6f},
        std::byte {0x6e}, std::byte {0x2c}, std::byte {0x20}, std::byte {0x32}, std::byte {0x31}, std::byte {0x20},
        std::byte {0x4f}, std::byte {0x63}, std::byte {0x74}, std::byte {0x20}, std::byte {0x32}, std::byte {0x30},
        std::byte {0x31}, std::byte {0x33}, std::byte {0x20}, std::byte {0x32}, std::byte {0x30}, std::byte {0x3a},
        std::byte {0x31}, std::byte {0x33}, std::byte {0x3a}, std::byte {0x32}, std::byte {0x32}, std::byte {0x20},
        std::byte {0x47}, std::byte {0x4d}, std::byte {0x54}, std::byte {0xc0}, std::byte {0x5a}, std::byte {0x04},
        std::byte {0x67}, std::byte {0x7a}, std::byte {0x69}, std::byte {0x70}, std::byte {0x77}, std::byte {0x38},
        std::byte {0x66}, std::byte {0x6f}, std::byte {0x6f}, std::byte {0x3d}, std::byte {0x41}, std::byte {0x53},
        std::byte {0x44}, std::byte {0x4a}, std::byte {0x4b}, std::byte {0x48}, std::byte {0x51}, std::byte {0x4b},
        std::byte {0x42}, std::byte {0x5a}, std::byte {0x58}, std::byte {0x4f}, std::byte {0x51}, std::byte {0x57},
        std::byte {0x45}, std::byte {0x4f}, std::byte {0x50}, std::byte {0x49}, std::byte {0x55}, std::byte {0x41},
        std::byte {0x58}, std::byte {0x51}, std::byte {0x57}, std::byte {0x45}, std::byte {0x4f}, std::byte {0x49},
        std::byte {0x55}, std::byte {0x3b}, std::byte {0x20}, std::byte {0x6d}, std::byte {0x61}, std::byte {0x78},
        std::byte {0x2d}, std::byte {0x61}, std::byte {0x67}, std::byte {0x65}, std::byte {0x3d}, std::byte {0x33},
        std::byte {0x36}, std::byte {0x30}, std::byte {0x30}, std::byte {0x3b}, std::byte {0x20}, std::byte {0x76},
        std::byte {0x65}, std::byte {0x72}, std::byte {0x73}, std::byte {0x69}, std::byte {0x6f}, std::byte {0x6e},
        std::byte {0x3d}, std::byte {0x31},
    };
    decoder.clear();
    EXPECT_TRUE(decoder.decode(response_data3).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 215);
    EXPECT_EQ(decoder.headerFieldList().size(), 6);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":status");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "200");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "private");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), "date");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "Mon, 21 Oct 2013 20:13:22 GMT");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), "location");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "https://www.example.com");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerName.c_str(), "content-encoding");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerValue.c_str(), "gzip");
    EXPECT_STREQ(decoder.headerFieldList()[5].headerName.c_str(), "set-cookie");
    EXPECT_STREQ(decoder.headerFieldList()[5].headerValue.c_str(),
                 "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1");

    encoder.clear();
    EXPECT_TRUE(encoder.encode(decoder.headerFieldList(), false).operator bool());
    ASSERT_EQ(encoder.buffer().size(), sizeof(response_data3));
    for (int i = 0; i < sizeof(response_data3); ++i) {
        EXPECT_EQ(encoder.buffer()[i], response_data3[i]) << "at index " << i;
    }
}

TEST(Hpack, ResponseWithHuffmanCoding) {
    HpackContext context;
    context.setLimitDynamicTableSize(256);
    HpackDecoder decoder(context);

    HpackContext context1;
    context1.setLimitDynamicTableSize(256);
    HpackEncoder encoder(context1);

    /*First Response
        :status: 302
        cache-control: private
        date: Mon, 21 Oct 2013 20:13:21 GMT
        location: https://www.example.com
    */
    std::byte response_data1[] = {
        std::byte {0x48}, std::byte {0x82}, std::byte {0x64}, std::byte {0x02}, std::byte {0x58}, std::byte {0x85},
        std::byte {0xae}, std::byte {0xc3}, std::byte {0x77}, std::byte {0x1a}, std::byte {0x4b}, std::byte {0x61},
        std::byte {0x96}, std::byte {0xd0}, std::byte {0x7a}, std::byte {0xbe}, std::byte {0x94}, std::byte {0x10},
        std::byte {0x54}, std::byte {0xd4}, std::byte {0x44}, std::byte {0xa8}, std::byte {0x20}, std::byte {0x05},
        std::byte {0x95}, std::byte {0x04}, std::byte {0x0b}, std::byte {0x81}, std::byte {0x66}, std::byte {0xe0},
        std::byte {0x82}, std::byte {0xa6}, std::byte {0x2d}, std::byte {0x1b}, std::byte {0xff}, std::byte {0x6e},
        std::byte {0x91}, std::byte {0x9d}, std::byte {0x29}, std::byte {0xad}, std::byte {0x17}, std::byte {0x18},
        std::byte {0x63}, std::byte {0xc7}, std::byte {0x8f}, std::byte {0x0b}, std::byte {0x97}, std::byte {0xc8},
        std::byte {0xe9}, std::byte {0xae}, std::byte {0x82}, std::byte {0xae}, std::byte {0x43}, std::byte {0xd3},
    };

    EXPECT_TRUE(decoder.decode(response_data1).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 222);
    EXPECT_EQ(decoder.headerFieldList().size(), 4);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":status");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "302");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "private");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), "date");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "Mon, 21 Oct 2013 20:13:21 GMT");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), "location");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "https://www.example.com");

    EXPECT_TRUE(encoder.encode(decoder.headerFieldList()).operator bool());
    EXPECT_EQ(encoder.buffer().size(), sizeof(response_data1));
    for (int i = 0; i < std::min(encoder.buffer().size(), sizeof(response_data1)); ++i) {
        EXPECT_EQ(encoder.buffer()[i], response_data1[i]) << "at index " << i;
    }

    /* Second response
        :status: 307
        cache-control: private
        date: Mon, 21 Oct 2013 20:13:21 GMT
        location: https://www.example.com
     */
    std::byte response_data2[] = {
        std::byte {0x48}, std::byte {0x83}, std::byte {0x64}, std::byte {0x0e},
        std::byte {0xff}, std::byte {0xc1}, std::byte {0xc0}, std::byte {0xbf},
    };
    decoder.clear();
    EXPECT_TRUE(decoder.decode(response_data2).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 222);
    EXPECT_EQ(decoder.headerFieldList().size(), 4);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":status");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "307");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "private");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), "date");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "Mon, 21 Oct 2013 20:13:21 GMT");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), "location");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "https://www.example.com");

    encoder.clear();
    EXPECT_TRUE(encoder.encode(decoder.headerFieldList()).operator bool());
    EXPECT_EQ(encoder.buffer().size(), sizeof(response_data2));
    for (int i = 0; i < std::min(encoder.buffer().size(), sizeof(response_data2)); ++i) {
        EXPECT_EQ(encoder.buffer()[i], response_data2[i]) << "at index " << i;
    }
    /* Third Response
        :status: 200
        cache-control: private
        date: Mon, 21 Oct 2013 20:13:22 GMT
        location: https://www.example.com
        content-encoding: gzip
        set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1
    */
    std::byte response_data3[] = {
        std::byte {0x88}, std::byte {0xc1}, std::byte {0x61}, std::byte {0x96}, std::byte {0xd0}, std::byte {0x7a},
        std::byte {0xbe}, std::byte {0x94}, std::byte {0x10}, std::byte {0x54}, std::byte {0xd4}, std::byte {0x44},
        std::byte {0xa8}, std::byte {0x20}, std::byte {0x05}, std::byte {0x95}, std::byte {0x04}, std::byte {0x0b},
        std::byte {0x81}, std::byte {0x66}, std::byte {0xe0}, std::byte {0x84}, std::byte {0xa6}, std::byte {0x2d},
        std::byte {0x1b}, std::byte {0xff}, std::byte {0xc0}, std::byte {0x5a}, std::byte {0x83}, std::byte {0x9b},
        std::byte {0xd9}, std::byte {0xab}, std::byte {0x77}, std::byte {0xad}, std::byte {0x94}, std::byte {0xe7},
        std::byte {0x82}, std::byte {0x1d}, std::byte {0xd7}, std::byte {0xf2}, std::byte {0xe6}, std::byte {0xc7},
        std::byte {0xb3}, std::byte {0x35}, std::byte {0xdf}, std::byte {0xdf}, std::byte {0xcd}, std::byte {0x5b},
        std::byte {0x39}, std::byte {0x60}, std::byte {0xd5}, std::byte {0xaf}, std::byte {0x27}, std::byte {0x08},
        std::byte {0x7f}, std::byte {0x36}, std::byte {0x72}, std::byte {0xc1}, std::byte {0xab}, std::byte {0x27},
        std::byte {0x0f}, std::byte {0xb5}, std::byte {0x29}, std::byte {0x1f}, std::byte {0x95}, std::byte {0x87},
        std::byte {0x31}, std::byte {0x60}, std::byte {0x65}, std::byte {0xc0}, std::byte {0x03}, std::byte {0xed},
        std::byte {0x4e}, std::byte {0xe5}, std::byte {0xb1}, std::byte {0x06}, std::byte {0x3d}, std::byte {0x50},
        std::byte {0x07},
    };
    decoder.clear();
    EXPECT_TRUE(decoder.decode(response_data3).operator bool());
    EXPECT_EQ(context.dynamicTableSize(), 215);
    EXPECT_EQ(decoder.headerFieldList().size(), 6);
    EXPECT_STREQ(decoder.headerFieldList()[0].headerName.c_str(), ":status");
    EXPECT_STREQ(decoder.headerFieldList()[0].headerValue.c_str(), "200");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerName.c_str(), "cache-control");
    EXPECT_STREQ(decoder.headerFieldList()[1].headerValue.c_str(), "private");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerName.c_str(), "date");
    EXPECT_STREQ(decoder.headerFieldList()[2].headerValue.c_str(), "Mon, 21 Oct 2013 20:13:22 GMT");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerName.c_str(), "location");
    EXPECT_STREQ(decoder.headerFieldList()[3].headerValue.c_str(), "https://www.example.com");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerName.c_str(), "content-encoding");
    EXPECT_STREQ(decoder.headerFieldList()[4].headerValue.c_str(), "gzip");
    EXPECT_STREQ(decoder.headerFieldList()[5].headerName.c_str(), "set-cookie");
    EXPECT_STREQ(decoder.headerFieldList()[5].headerValue.c_str(),
                 "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1");

    encoder.clear();
    EXPECT_TRUE(encoder.encode(decoder.headerFieldList()).operator bool());
    EXPECT_EQ(encoder.buffer().size(), sizeof(response_data3));
    for (int i = 0; i < std::min(encoder.buffer().size(), sizeof(response_data3)); ++i) {
        EXPECT_EQ(encoder.buffer()[i], response_data3[i]) << "at index " << i;
    }
}

TEST(Hpack, DictionaryTree) {
    DictionaryTree<int> tree;
    tree.insert("foo", 1);
    EXPECT_EQ(tree.find("foo"), 1);
    EXPECT_EQ(tree.find("bar"), std::nullopt);
    tree.insert("bar", 2);
    EXPECT_EQ(tree.find("bar"), 2);
    tree.remove("foo");
    EXPECT_EQ(tree.find("foo"), std::nullopt);
    EXPECT_EQ(tree.find("bar"), 2);
    tree.remove("bar");
    EXPECT_EQ(tree.find("bar"), std::nullopt);
    tree.insert("a", 1);
    tree.insert("aa", 2);
    tree.insert("aaa", 3);
    EXPECT_EQ(tree.find("a"), 1);
    EXPECT_EQ(tree.find("aa"), 2);
    EXPECT_EQ(tree.find("aaa"), 3);
    EXPECT_EQ(tree.find("aaaa"), std::nullopt);
    tree.remove("a");
    EXPECT_EQ(tree.find("a"), std::nullopt);
    EXPECT_EQ(tree.find("aa"), 2);
    EXPECT_EQ(tree.find("aaa"), 3);
    EXPECT_EQ(tree.find("aaaa"), std::nullopt);
    tree.remove("aa");
    EXPECT_EQ(tree.find("aa"), std::nullopt);
    EXPECT_EQ(tree.find("aaa"), 3);
    EXPECT_EQ(tree.find("aaaa"), std::nullopt);
    tree.remove("aaa");
    EXPECT_EQ(tree.find("aaa"), std::nullopt);

    DictionaryTree<int, 2> tree2;
    tree2.setZero('0');
    tree2.insert("0", 1);
    EXPECT_EQ(tree2.find("0"), 1);
    EXPECT_EQ(tree2.find("1"), std::nullopt);
    tree2.insert("1", 2);
    EXPECT_EQ(tree2.find("1"), 2);
    EXPECT_EQ(tree2.find("0"), 1);
    tree2.insert("01", 3);
    EXPECT_EQ(tree2.find("01"), 3);
    tree2.insert("001", 4);
    EXPECT_EQ(tree2.find("001"), 4);
    EXPECT_EQ(tree2.find("000"), std::nullopt);
    EXPECT_EQ(tree2.find("1"), 2);
    tree2.remove("01");
    tree2.remove("0");
    EXPECT_EQ(tree2.find("0"), std::nullopt);
    EXPECT_EQ(tree2.find("1"), 2);
    EXPECT_EQ(tree2.find("01"), std::nullopt);
    EXPECT_EQ(tree2.find("001"), 4);
    EXPECT_EQ(tree2.find(1U, 3), 4);
    EXPECT_EQ(tree2.find(1U, 1), 2);

    tree2.clear();
    EXPECT_EQ(tree2.find("0"), std::nullopt);

    tree2.insert(0b00010111, 1, 8);
    EXPECT_EQ(tree2.find(0b00010111, 8), 1);
    EXPECT_EQ(tree2.find(0b00010111, 7), std::nullopt);
    EXPECT_EQ(tree2.find(0b00010111, 1), std::nullopt);
    tree2.insert(0b00011111, 2, 8);
    EXPECT_EQ(tree2.find(0b00011111, 8), 2);
    EXPECT_EQ(tree2.find(0b00011111, 7), std::nullopt);
    tree2.insert(0b0001111, 3, 7);
    tree2.insert(0b000111, 4, 6);
    EXPECT_EQ(tree2.find(0b000111, 6), 4);
    tree2.remove(0b000111, 6);
    EXPECT_EQ(tree2.find(0b000111, 6), std::nullopt);
    EXPECT_EQ(tree2.find(0b0001111, 7), 3);
    EXPECT_EQ(tree2.find(0b00011111, 8), 2);
    EXPECT_EQ(tree2.size(), 3);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}