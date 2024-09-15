#include <ilias/net/endpoint.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(Endpoint, Parse4) {
    IPEndpoint endpoint1("127.0.0.1:8080");
    EXPECT_TRUE(endpoint1.isValid());
    EXPECT_EQ(endpoint1.address(), "127.0.0.1");
    EXPECT_EQ(endpoint1.port(), 8080);
    std::cout << endpoint1.toString() << std::endl;

    IPEndpoint endpoint2("127.0.0.1:11451");
    EXPECT_TRUE(endpoint2.isValid());
    EXPECT_EQ(endpoint2.address(), "127.0.0.1");
    EXPECT_EQ(endpoint2.port(), 11451);
    std::cout << endpoint2.toString() << std::endl;


    IPEndpoint endpoint3("127.0.0.1:65535");
    EXPECT_TRUE(endpoint3.isValid());
    EXPECT_EQ(endpoint3.address(), "127.0.0.1");
    EXPECT_EQ(endpoint3.port(), 65535);
    std::cout << endpoint3.toString() << std::endl;

    // False test cases
    IPEndpoint endpoint4("127.0.0.1:65536");
    EXPECT_FALSE(endpoint4.isValid());
    std::cout << endpoint4.toString() << std::endl;

    IPEndpoint endpoint5("127.0.0.1:8080:8080");
    EXPECT_FALSE(endpoint5.isValid());
    std::cout << endpoint5.toString() << std::endl;

    IPEndpoint endpoint6("127asdlllll:askasjajskajs");
    EXPECT_FALSE(endpoint6.isValid());
    std::cout << endpoint6.toString() << std::endl;
}

TEST(Endpoint, Parse6) {
    IPEndpoint endpoint1("[::1]:8080");
    EXPECT_TRUE(endpoint1.isValid());
    EXPECT_EQ(endpoint1.address(), "::1");
    EXPECT_EQ(endpoint1.port(), 8080);
    std::cout << endpoint1.toString() << std::endl;

    IPEndpoint endpoint2("[::1]:11451");
    EXPECT_TRUE(endpoint2.isValid());
    EXPECT_EQ(endpoint2.address(), "::1");
    EXPECT_EQ(endpoint2.port(), 11451);
    std::cout << endpoint2.toString() << std::endl;

    IPEndpoint endpoint3("[::1]:65535");
    EXPECT_TRUE(endpoint3.isValid());
    EXPECT_EQ(endpoint3.address(), "::1");
    EXPECT_EQ(endpoint3.port(), 65535);
    std::cout << endpoint3.toString() << std::endl;

    // False test cases
    IPEndpoint endpoint4("[::1]:65536");
    EXPECT_FALSE(endpoint4.isValid());
    std::cout << endpoint4.toString() << std::endl;

    IPEndpoint endpoint5("[askasjajskajs]:8080");
    EXPECT_FALSE(endpoint5.isValid());
    std::cout << endpoint5.toString() << std::endl;
}

TEST(Endpoint, ToString) {
    IPEndpoint endpoint(IPAddress4::any(), 8080);
    EXPECT_EQ(endpoint.toString(), "0.0.0.0:8080");

#if defined(__cpp_lib_format)
    EXPECT_EQ(std::format("{}", endpoint), "0.0.0.0:8080");
#endif

    IPEndpoint endpoint2(IPAddress6::none(), 8080);
    EXPECT_EQ(endpoint2.toString(), "[::]:8080");

#if defined(__cpp_lib_format)
    EXPECT_EQ(std::format("{}", endpoint2), "[::]:8080");
#endif
}

auto main(int argc, char **argv) -> int {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}