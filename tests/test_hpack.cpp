#include <gtest/gtest.h>

#include "../include/ilias/http/detail/hpack.hpp"

using Ilias::http2::detail::HpackContext;
using Ilias::http2::detail::HpackDecoder;
using Ilias::http2::detail::HpackError;

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
    std::byte input[10];
    std::byte output[10];
    
}

TEST(Hpack, IntDecoderTest) {
    HpackContext context;
    std::byte    buffer[1];
    buffer[0] = std::byte {0xf2};
    HpackDecoder decoder(context, buffer);
    int          value;
    EXPECT_EQ(decoder.getInt(value), 1);
    EXPECT_EQ(value, 242);

    std::byte buffer2[5] = {std::byte {0xff}, std::byte {0xf2}, std::byte {0x83}, std::byte {0xf4}, std::byte {0x7f}};
    decoder.resetBuffer(buffer2);
    EXPECT_EQ(decoder.getInt(value), 5);
    EXPECT_EQ(value, 268239346);

    std::byte buffer3[6] = {std::byte {0xff}, std::byte {0xf2}, std::byte {0x83},
                            std::byte {0xf4}, std::byte {0x8f}, std::byte {0x70}};
    decoder.resetBuffer(buffer3);
    EXPECT_EQ(decoder.getInt(value), -1);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}