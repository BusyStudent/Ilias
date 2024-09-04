#include <ilias/url.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(UrlTest, ValidUrl) {
    Url url("www.google.com");
    ASSERT_EQ(url.port(), std::nullopt);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/");
    ASSERT_EQ(url.toString(), "www.google.com");
    ASSERT_TRUE(url.isValid());

    url = "https://www.google.com";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), std::nullopt);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/");
    ASSERT_EQ(url.toString(), "https://www.google.com");
    ASSERT_TRUE(url.isValid());

    url = "https://www.google.com:10086";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 10086);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/");
    ASSERT_TRUE(url.isValid());

    url = "https://www.google.com:10086/path";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 10086);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/path");
    ASSERT_TRUE(url.isValid());

    url = "https://www.google.com/path";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), std::nullopt);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/path");
    ASSERT_TRUE(url.isValid());

    url = "127.0.0.4:123";
    ASSERT_EQ(url.host(), "127.0.0.4");
    ASSERT_EQ(url.port(), 123);
    ASSERT_EQ(url.path(), "/");
    ASSERT_TRUE(url.isValid());

    url = "https://cn.aliyun.com/";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), std::nullopt);
    ASSERT_EQ(url.host(), "cn.aliyun.com");
    ASSERT_EQ(url.path(), "/");

    // TODO: IPV6
    // url = "http://[2001:db8::1]:";
    // ASSERT_EQ(url.scheme(), "http");
    // ASSERT_EQ(url.port(), 80);
    // ASSERT_EQ(url.host(), "[2001:db8::1]");

    url = "https://www.example.com/path?param=value%20with%20spaces";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.host(), "www.example.com");
    ASSERT_EQ(url.path(), "/path");
    ASSERT_EQ(url.query(), "param=value%20with%20spaces");
    ASSERT_TRUE(url.isValid());

    url.setHost("example/aaa.com");
    ASSERT_FALSE(url.isValid());
}

TEST(UrlTest, Encode) {
    ASSERT_EQ(Url::encodeComponent("Hello, World!"), "Hello%2C%20World%21");
    ASSERT_EQ(Url::decodeComponent("Hello%2C%20World%21"), "Hello, World!");
    ASSERT_EQ(Url::decodeComponent("Hello%2C%20World%21%3F%3F"), "Hello, World!??");

    // TEST unicode
    ASSERT_EQ(Url::encodeComponent("你好，世界！"), "%E4%BD%A0%E5%A5%BD%EF%BC%8C%E4%B8%96%E7%95%8C%EF%BC%81");
    ASSERT_EQ(Url::decodeComponent("%E4%BD%A0%E5%A5%BD%EF%BC%8C%E4%B8%96%E7%95%8C%EF%BC%81"), "你好，世界！");
}


auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}