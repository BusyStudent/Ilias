#include <gtest/gtest.h>
#include "../include/ilias_http.hpp"
#include <fstream>

#ifdef _WIN32
    #include "../include/ilias_iocp.hpp"
    #include "../include/ilias_iocp.cpp"
#else
    #include "../include/ilias_poll.hpp"
#endif

using namespace ILIAS_NAMESPACE;

TEST(UrlTest, ValidUrl) {
    Url url("www.google.com");
    ASSERT_EQ(url.port(), 0);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/");

    url = "https://www.google.com";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 443);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/");

    url = "https://www.google.com:10086";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 10086);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/");

    url = "https://www.google.com:10086/path";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 10086);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/path");

    url = "https://www.google.com/path";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 443);
    ASSERT_EQ(url.host(), "www.google.com");
    ASSERT_EQ(url.path(), "/path");

    url = "127.0.0.4:123";
    ASSERT_EQ(url.host(), "127.0.0.4");
    ASSERT_EQ(url.port(), 123);
    ASSERT_EQ(url.path(), "/");

    url = "https://cn.aliyun.com/";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 443);
    ASSERT_EQ(url.host(), "cn.aliyun.com");
    ASSERT_EQ(url.path(), "/");

    // TODO: IPV6
    // url = "http://[2001:db8::1]:";
    // ASSERT_EQ(url.scheme(), "http");
    // ASSERT_EQ(url.port(), 80);
    // ASSERT_EQ(url.host(), "[2001:db8::1]");

    url = "https://www.example.com/path?param=value%20with%20spaces";
    ASSERT_EQ(url.scheme(), "https");
    ASSERT_EQ(url.port(), 443);
    ASSERT_EQ(url.host(), "www.example.com");
    ASSERT_EQ(url.path(), "/path");
    ASSERT_EQ(url.query(), "param=value%20with%20spaces");
}

static HttpSession *session = nullptr;

#if 0
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
#endif

TEST(HttpBinRequest, GET) {
    auto reply = ilias_wait session->get("https://httpbin.org/get");
    EXPECT_TRUE(reply);
    EXPECT_EQ(reply->statusCode(), 200);
    auto text = ilias_wait reply->text();
    EXPECT_TRUE(text);

    std::cout << text.value_or("READ FAILED") << std::endl;
}

TEST(HttpBinRequest, POST) {
    auto reply = ilias_wait session->post("https://httpbin.org/post", "Hello, World!");
    EXPECT_TRUE(reply);
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


int main(int argc, char **argv) {

#if defined(_WIN32)
    IOCPContext ctxt;
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#else
    PollContext ctxt;
#endif

    HttpCookieJar jar;
    HttpSession _session;
    _session.setCookieJar(&jar);
    session = &_session;

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}