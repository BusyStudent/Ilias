#include <ilias/http/cookie.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(CookieTest, Parse) {
    auto cookies = HttpCookie::parse("foo=bar; baz=qux");
    ASSERT_EQ(cookies.size(), 2);
    ASSERT_EQ("foo", cookies[0].name());
    ASSERT_EQ("bar", cookies[0].value());
    ASSERT_EQ("baz", cookies[1].name());
    ASSERT_EQ("qux", cookies[1].value());
}

TEST(CookieTest, Parse2) {
    auto cookies = HttpCookie::parse("foo=bar; baz=qux; Secure; SameSite=Strict");
    ASSERT_EQ(cookies.size(), 2);
    ASSERT_EQ("foo", cookies[0].name());
    ASSERT_EQ("bar", cookies[0].value());
    ASSERT_EQ("baz", cookies[1].name());
    ASSERT_EQ("qux", cookies[1].value());
    ASSERT_TRUE(cookies[0].isSecure());
    ASSERT_TRUE(cookies[1].isSecure());
    ASSERT_EQ(cookies[0].sameSite(), HttpCookie::Strict);
    ASSERT_EQ(cookies[1].sameSite(), HttpCookie::Strict);
}

TEST(CookieJar, Match) {
    HttpCookie cookie1;
    cookie1.setDomain(".example.com");
    cookie1.setPath("/");
    cookie1.setName("foo");
    cookie1.setValue("bar");

    HttpCookie cookie2;
    cookie2.setDomain("www.example.com");
    cookie2.setPath("/");
    cookie2.setName("aaa");
    cookie2.setValue("bbb");

    HttpCookie cookie3;
    cookie3.setDomain("WWW.EXAMPLE.COM");
    cookie3.setName("ccc");
    cookie3.setValue("ddd");

    HttpCookieJar jar;
    jar.insertCookie(cookie1);
    jar.insertCookie(cookie2);
    jar.insertCookie(cookie3);

    auto cookies = jar.cookiesForUrl("http://www.example.com/");
    ASSERT_EQ(cookies.size(), 3);
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}