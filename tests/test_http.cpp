#include "../include/ilias/networking.hpp"
#include "../include/ilias/http.hpp"
#include <gtest/gtest.h>
#include <fstream>

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


static HttpSession *session = nullptr;

#if 1

TEST(RequestTest, Test1) {
    HttpRequest request("https://www.baidu.com");

    // auto ret = ilias_wait session.get(request);
    // // auto ret = ilias_wait session.get("https://www.bilibili.com");
    // if (ret) {
    //     auto text = ilias_wait ret->text();
    //     std::cout << text.value_or("READ FAILED") << std::endl;
    // }
    HttpRequest request2("https://www.bilibili.com");
    request2.setHeader(HttpHeaders::UserAgent, "vscode-restclient");
    request2.setHeader(HttpHeaders::Accept, "*/*");
    request2.setHeader(HttpHeaders::Referer, "https://www.bilibili.com/");
    auto ret2 = ilias_wait session->get(request2);
    if (ret2) {
        auto text = ilias_wait ret2->text();
        std::cout << text.value_or("READ FAILED") << std::endl;
    }
}

TEST(HttpBinRequest, GET) {
    auto reply = ilias_wait session->get("https://httpbin.org/get");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->statusCode(), 200);
    auto text = ilias_wait reply->text();
    EXPECT_TRUE(text);

    std::cout << text.value_or("READ FAILED") << std::endl;
}

TEST(HttpBinRequest, POST) {
    auto reply = ilias_wait session->post("https://httpbin.org/post", "Hello, World!");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->statusCode(), 200);
    auto text = ilias_wait reply->text();
    EXPECT_TRUE(text);

    std::cout << text.value_or("READ FAILED") << std::endl;
}

#if !defined(ILIAS_NO_ZLIB)
TEST(HttpBinRequest, Gzip) {
    auto reply = ilias_wait session->get("https://httpbin.org/gzip");
    EXPECT_TRUE(reply);
    EXPECT_EQ(reply->statusCode(), 200);
    auto text = ilias_wait reply->text();
    EXPECT_TRUE(text);

    std::cout << text.value_or("READ FAILED") << std::endl;
}

TEST(HttpBinRequest, Deflate) {
    auto reply = ilias_wait session->get("https://httpbin.org/deflate");
    EXPECT_TRUE(reply);
    EXPECT_EQ(reply->statusCode(), 200);
    auto text = ilias_wait reply->text();
    EXPECT_TRUE(text);

    std::cout << text.value_or("READ FAILED") << std::endl;
}
#endif

#endif

int main(int argc, char **argv) {

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif
    PlatformIoContext ctxt;

    HttpCookieJar jar;
    HttpSession _session;
    _session.setCookieJar(&jar);
    session = &_session;

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}