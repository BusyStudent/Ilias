#include <ilias/platform/platform.hpp>
#include <ilias/http/session.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

HttpSession *session = nullptr;

TEST(Session, GET) {
    auto reply = session->get("https://www.baidu.com").wait();
    ASSERT_TRUE(reply);
    auto text = reply->text().wait();
    ASSERT_TRUE(text);
    std::cout << text.value() << std::endl;

    auto reply2 = session->get("http://www.bilibili.com").wait();
    ASSERT_TRUE(reply2);
    auto text2 = reply2->text().wait();
    ASSERT_TRUE(text2);
    std::cout << text2.value() << std::endl;
}

TEST(Session, HEAD) {
    auto reply = session->head("https://www.baidu.com").wait();
    ASSERT_TRUE(reply);
    auto text = reply->text().wait();
    ASSERT_TRUE(text);
    ASSERT_TRUE(text.value().empty());

    auto reply2 = session->head("http://www.bilibili.com").wait();
    ASSERT_TRUE(reply2);
    auto text2 = reply2->text().wait();
    ASSERT_TRUE(text2);
}

auto main(int argc, char **argv) -> int {

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif

    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);

    PlatformContext ctxt;
    HttpCookieJar jar;
    HttpSession httpSession(ctxt);
    session = &httpSession;
    session->setCookieJar(&jar);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}