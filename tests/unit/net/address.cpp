#include <ilias/net/address.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

// For V4 Address
TEST(Address4, Parse) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0").value(), IPAddress4::any());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value(), IPAddress4::none());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value(), IPAddress4::broadcast());
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1").value(), IPAddress4::loopback());

    EXPECT_FALSE(IPAddress4::fromString("::1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("::").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0.1:8080").has_value());
    EXPECT_FALSE(IPAddress4::fromString("256.256.256.256").has_value());
}

TEST(Address4, ToString) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0").value().toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value().toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1").value().toString(), "127.0.0.1");

    EXPECT_EQ(IPAddress4::any().toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::broadcast().toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::loopback().toString(), "127.0.0.1");

#if defined(__cpp_lib_format)
    EXPECT_EQ(std::format("{}", IPAddress4::any()), "0.0.0.0");
    EXPECT_EQ(std::format("{}", IPAddress4::broadcast()), "255.255.255.255");
    EXPECT_EQ(std::format("{}", IPAddress4::loopback()), "127.0.0.1");
#endif
}

TEST(Address4, Span) {
    auto addr = IPAddress4::none();
    auto span = addr.span();
    EXPECT_EQ(span[0], std::byte {255});
    EXPECT_EQ(span[1], std::byte {255});
    EXPECT_EQ(span[2], std::byte {255});
    EXPECT_EQ(span[3], std::byte {255});
}

// For V6 Address
TEST(Address6, Parse) {
    EXPECT_EQ(IPAddress6::fromString("::1").value(), IPAddress6::loopback());
    EXPECT_EQ(IPAddress6::fromString("::").value(), IPAddress6::any());

    EXPECT_FALSE(IPAddress6::fromString("0.0.0.0").has_value());
    EXPECT_FALSE(IPAddress6::fromString("255.255.255.255").has_value());
    EXPECT_FALSE(IPAddress6::fromString("127.0.0.1").has_value());
    EXPECT_FALSE(IPAddress6::fromString("127.0.0.1:8080").has_value());
    EXPECT_FALSE(IPAddress6::fromString("256.256.256.256").has_value());
}

// For V4 / 6 Address
TEST(Address, Parse) {
    EXPECT_EQ(IPAddress("0.0.0.0").family(), AF_INET);
    EXPECT_EQ(IPAddress("255.255.255.255").family(), AF_INET);
    EXPECT_EQ(IPAddress("127.0.0.1").family(), AF_INET);
    
    EXPECT_EQ(IPAddress("::1").family(), AF_INET6);
    EXPECT_EQ(IPAddress("::").family(), AF_INET6);
    EXPECT_EQ(IPAddress("::ffff:192.168.1.1").family(), AF_INET6);

    EXPECT_FALSE(IPAddress::fromString("127.0.0.1:8080").has_value());
    EXPECT_FALSE(IPAddress::fromString("256.256.256.256").has_value());
    EXPECT_FALSE(IPAddress::fromString("::ffff:256.256.256.256").has_value());
}

TEST(Address, ToString) {
    EXPECT_EQ(IPAddress(IPAddress4::any()).toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress(IPAddress4::none()).toString(), "255.255.255.255");

}

auto main() -> int {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}