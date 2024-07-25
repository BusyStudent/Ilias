#include <gtest/gtest.h>

#include "../include/ilias/http/detail/hpack.hpp"
#include "../include/ilias/http/detail/dictionary_tree.hpp"

using namespace Ilias::http2::detail;

TEST(Hpack, ContextTestStaticTable) {
    HpackContext context;
    {
        auto field = context.indexToNameValuePair(1).value();
        EXPECT_STREQ(field.headerName.data(), ":authority");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(2).value();
        EXPECT_STREQ(field.headerName.data(), ":method");
        EXPECT_STREQ(field.headerValue.data(), "GET");
    }
    {
        auto field = context.indexToNameValuePair(3).value();
        EXPECT_STREQ(field.headerName.data(), ":method");
        EXPECT_STREQ(field.headerValue.data(), "POST");
    }
    {
        auto field = context.indexToNameValuePair(4).value();
        EXPECT_STREQ(field.headerName.data(), ":path");
        EXPECT_STREQ(field.headerValue.data(), "/");
    }
    {
        auto field = context.indexToNameValuePair(5).value();
        EXPECT_STREQ(field.headerName.data(), ":path");
        EXPECT_STREQ(field.headerValue.data(), "/index.html");
    }
    {
        auto field = context.indexToNameValuePair(6).value();
        EXPECT_STREQ(field.headerName.data(), ":scheme");
        EXPECT_STREQ(field.headerValue.data(), "http");
    }
    {
        auto field = context.indexToNameValuePair(7).value();
        EXPECT_STREQ(field.headerName.data(), ":scheme");
        EXPECT_STREQ(field.headerValue.data(), "https");
    }
    {
        auto field = context.indexToNameValuePair(8).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "200");
    }
    {
        auto field = context.indexToNameValuePair(9).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "204");
    }
    {
        auto field = context.indexToNameValuePair(10).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "206");
    }
    {
        auto field = context.indexToNameValuePair(11).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "304");
    }
    {
        auto field = context.indexToNameValuePair(12).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "400");
    }
    {
        auto field = context.indexToNameValuePair(13).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "404");
    }
    {
        auto field = context.indexToNameValuePair(14).value();
        EXPECT_STREQ(field.headerName.data(), ":status");
        EXPECT_STREQ(field.headerValue.data(), "500");
    }
    {
        auto field = context.indexToNameValuePair(15).value();
        EXPECT_STREQ(field.headerName.data(), "accept-charset");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(16).value();
        EXPECT_STREQ(field.headerName.data(), "accept-encoding");
        EXPECT_STREQ(field.headerValue.data(), "gzip, deflate");
    }
    {
        auto field = context.indexToNameValuePair(17).value();
        EXPECT_STREQ(field.headerName.data(), "accept-language");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(18).value();
        EXPECT_STREQ(field.headerName.data(), "accept-ranges");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(19).value();
        EXPECT_STREQ(field.headerName.data(), "accept");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(20).value();
        EXPECT_STREQ(field.headerName.data(), "access-control-allow-origin");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(21).value();
        EXPECT_STREQ(field.headerName.data(), "age");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(22).value();
        EXPECT_STREQ(field.headerName.data(), "allow");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(23).value();
        EXPECT_STREQ(field.headerName.data(), "authorization");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(24).value();
        EXPECT_STREQ(field.headerName.data(), "cache-control");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(25).value();
        EXPECT_STREQ(field.headerName.data(), "content-disposition");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(26).value();
        EXPECT_STREQ(field.headerName.data(), "content-encoding");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(27).value();
        EXPECT_STREQ(field.headerName.data(), "content-language");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(28).value();
        EXPECT_STREQ(field.headerName.data(), "content-length");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(29).value();
        EXPECT_STREQ(field.headerName.data(), "content-location");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(30).value();
        EXPECT_STREQ(field.headerName.data(), "content-range");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(31).value();
        EXPECT_STREQ(field.headerName.data(), "content-type");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(32).value();
        EXPECT_STREQ(field.headerName.data(), "cookie");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(33).value();
        EXPECT_STREQ(field.headerName.data(), "date");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(34).value();
        EXPECT_STREQ(field.headerName.data(), "etag");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(35).value();
        EXPECT_STREQ(field.headerName.data(), "expect");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(36).value();
        EXPECT_STREQ(field.headerName.data(), "expires");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(37).value();
        EXPECT_STREQ(field.headerName.data(), "from");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(38).value();
        EXPECT_STREQ(field.headerName.data(), "host");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(39).value();
        EXPECT_STREQ(field.headerName.data(), "if-match");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(40).value();
        EXPECT_STREQ(field.headerName.data(), "if-modified-since");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(41).value();
        EXPECT_STREQ(field.headerName.data(), "if-none-match");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(42).value();
        EXPECT_STREQ(field.headerName.data(), "if-range");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(43).value();
        EXPECT_STREQ(field.headerName.data(), "if-unmodified-since");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(44).value();
        EXPECT_STREQ(field.headerName.data(), "last-modified");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(45).value();
        EXPECT_STREQ(field.headerName.data(), "link");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(46).value();
        EXPECT_STREQ(field.headerName.data(), "location");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(47).value();
        EXPECT_STREQ(field.headerName.data(), "max-forwards");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(48).value();
        EXPECT_STREQ(field.headerName.data(), "proxy-authenticate");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(49).value();
        EXPECT_STREQ(field.headerName.data(), "proxy-authorization");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(50).value();
        EXPECT_STREQ(field.headerName.data(), "range");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(51).value();
        EXPECT_STREQ(field.headerName.data(), "referer");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(52).value();
        EXPECT_STREQ(field.headerName.data(), "refresh");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(53).value();
        EXPECT_STREQ(field.headerName.data(), "retry-after");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(54).value();
        EXPECT_STREQ(field.headerName.data(), "server");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(55).value();
        EXPECT_STREQ(field.headerName.data(), "set-cookie");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(56).value();
        EXPECT_STREQ(field.headerName.data(), "strict-transport-security");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(57).value();
        EXPECT_STREQ(field.headerName.data(), "transfer-encoding");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(58).value();
        EXPECT_STREQ(field.headerName.data(), "user-agent");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(59).value();
        EXPECT_STREQ(field.headerName.data(), "vary");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(60).value();
        EXPECT_STREQ(field.headerName.data(), "via");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
    {
        auto field = context.indexToNameValuePair(61).value();
        EXPECT_STREQ(field.headerName.data(), "www-authenticate");
        EXPECT_STREQ(field.headerValue.data(), "");
    }
}

TEST(Hpack, ContextDynamicTable) {
    HpackContext context;
    context.addNameValuePair("custom-header1", "custom-value");
    context.addNameValuePair("custom-header1", "custom-value1");
    context.addNameValuePair("custom-header3", "custom-value3");
    EXPECT_EQ(context.dynamicTableSize(), 176);

    auto field = context.indexToNameValuePair(62).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header3");
    EXPECT_STREQ(field.headerValue.data(), "custom-value3");
    field = context.indexToNameValuePair(63).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header1");
    EXPECT_STREQ(field.headerValue.data(), "custom-value1");

    context.setMaxDynamicTableSize(70);
    EXPECT_EQ(context.dynamicTableSize(), 59);
    field = context.indexToNameValuePair(62).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header3");
    EXPECT_STREQ(field.headerValue.data(), "custom-value3");

    context.addNameValuePair(
        "custom-header1", "a very very big value for this test,                                                     "
                          "                                                                                         ");
    auto ret = context.indexToNameValuePair(62);
    EXPECT_EQ(ret.error(), HpackError::InvalidIndexForDecodingTable);

    context.addNameValuePair("custom-header1", "custom-value1");
    EXPECT_EQ(context.dynamicTableSize(), 59);
    field = context.indexToNameValuePair(62).value();
    EXPECT_STREQ(field.headerName.data(), "custom-header1");
    EXPECT_STREQ(field.headerValue.data(), "custom-value1");
    context.setMaxDynamicTableSize(0);
    EXPECT_EQ(context.dynamicTableSize(), 0);
}

TEST(Hpack, HuffmanCodeTest) {
    std::byte input[10] = {std::byte {'a'}, std::byte {'b'}, std::byte {'c'}, std::byte {'d'}, std::byte {'e'}};

    std::vector<std::byte> outputBuffer;
    HuffmanEncoder         encoder;
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
    HuffmanDecoder         decoder;
    decoder.decode(outputBuffer, outputBuffer2);
    ASSERT_EQ(outputBuffer2.size(), 5);
    EXPECT_EQ(memcmp(outputBuffer2.data(), input, 5), 0);
}

TEST(Hpack, IntDecoderTest) {
    HpackContext context;
    std::byte    buffer[1];
    buffer[0] = std::byte {0xf2};
    HpackDecoder decoder(context);
    int          value;
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

TEST(Hpack, EncoderDecoder) {
    HpackContext context;
    HpackEncoder encoder(context);

    std::string str_data      = "Hello, World!";
    std::byte   encode_data[] = {std::byte {0x0D}, std::byte {'H'}, std::byte {'e'}, std::byte {'l'}, std::byte {'l'},
                                 std::byte {'o'},  std::byte {','}, std::byte {' '}, std::byte {'W'}, std::byte {'o'},
                                 std::byte {'r'},  std::byte {'l'}, std::byte {'d'}, std::byte {'!'}};
    EXPECT_TRUE(encoder.saveString(str_data).has_value());
    EXPECT_EQ(encoder.buffer().size(), 14);
    for (size_t i = 0; i < 14; ++i) {
        EXPECT_EQ(encoder.buffer()[i], encode_data[i]);
    }

    HpackDecoder decoder(context);
    std::string  str_data2;
    EXPECT_EQ(decoder.getString(encode_data, str_data2).value(), 14);
    EXPECT_STREQ(str_data2.c_str(), str_data.c_str());

    encoder.reset();
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