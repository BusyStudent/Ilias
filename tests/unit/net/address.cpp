#include <ilias/net/address.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

// For V4 Address
TEST(Address4, Parse) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0").value(), IPAddress4::any());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value(), IPAddress4::none());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value(), IPAddress4::broadcast());
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1").value(), IPAddress4::loopback());

    // Fail cases
    EXPECT_FALSE(IPAddress4::fromString("::1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("::").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0.1:8080").has_value());
    EXPECT_FALSE(IPAddress4::fromString("256.256.256.256").has_value());

    EXPECT_FALSE(IPAddress4::fromString("127x0.0.1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0.1x").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0x1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0x.1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.x.0.1").has_value());
    EXPECT_FALSE(IPAddress4::fromString("127.0.0.1.").has_value());

    EXPECT_FALSE(IPAddress4::fromString("的贷记卡就是").has_value());
    EXPECT_FALSE(IPAddress4::fromString("114.114.114.114.114.114.114.114").has_value());
}

TEST(Address4, ToString) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0").value().toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").value().toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1").value().toString(), "127.0.0.1");

    EXPECT_EQ(IPAddress4::any().toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::broadcast().toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::loopback().toString(), "127.0.0.1");

#if !defined(ILIAS_NO_FORMAT)
    EXPECT_EQ(fmtlib::format("{}", IPAddress4::any()), "0.0.0.0");
    EXPECT_EQ(fmtlib::format("{}", IPAddress4::broadcast()), "255.255.255.255");
    EXPECT_EQ(fmtlib::format("{}", IPAddress4::loopback()), "127.0.0.1");
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

TEST(Address4, Compare) {
    EXPECT_EQ(IPAddress4::none(), IPAddress4::none());
    EXPECT_NE(IPAddress4::none(), IPAddress4::any());
    EXPECT_NE(IPAddress4::none(), IPAddress4::loopback());
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
    EXPECT_FALSE(IPAddress6::fromString("::ffff:256.256.256.256").has_value());
    EXPECT_FALSE(IPAddress6::fromString("asdkljakldjasdnm,sa南萨摩").has_value());
    EXPECT_FALSE(IPAddress6::fromString("::ffff:1121212121:121212:sa1212121211212121212121:12121212121:as2a1s2a1212").has_value());
}

TEST(Address6, Compare) {
    EXPECT_EQ(IPAddress6::loopback(), IPAddress6::loopback());
    EXPECT_NE(IPAddress6::loopback(), IPAddress6::any());
    EXPECT_NE(IPAddress6::loopback(), IPAddress6::none());
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

TEST(Address, Compare) {
    EXPECT_EQ(IPAddress(), IPAddress());
    EXPECT_EQ(IPAddress(IPAddress4::any()), IPAddress(IPAddress4::any()));
    EXPECT_NE(IPAddress(IPAddress4::any()), IPAddress(IPAddress4::none()));
    EXPECT_EQ(IPAddress(IPAddress6::loopback()), IPAddress(IPAddress6::loopback()));
    EXPECT_NE(IPAddress(IPAddress6::loopback()), IPAddress(IPAddress6::any()));
    EXPECT_NE(IPAddress(IPAddress4::loopback()), IPAddress(IPAddress6::none()));
    EXPECT_NE(IPAddress(IPAddress4::loopback()), IPAddress());
}

auto main() -> int {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}