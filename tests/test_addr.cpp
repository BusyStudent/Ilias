#include "../include/ilias_inet.hpp"
#include <gtest/gtest.h>
#include <iostream>

using namespace ILIAS_NAMESPACE;

// For V4 Address
TEST(Address4, Parse) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0"), IPAddress4::any());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255"), IPAddress4::none());
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255"), IPAddress4::broadcast());
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1"), IPAddress4::loopback());
}

TEST(Address4, ToString) {
    EXPECT_EQ(IPAddress4::fromString("0.0.0.0").toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::fromString("255.255.255.255").toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::fromString("127.0.0.1").toString(), "127.0.0.1");

    EXPECT_EQ(IPAddress4::any().toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress4::broadcast().toString(), "255.255.255.255");
    EXPECT_EQ(IPAddress4::loopback().toString(), "127.0.0.1");
}

TEST(Address4, Span) {
    auto addr = IPAddress4::none();
    auto span = addr.span<uint8_t>();
    EXPECT_EQ(span[0], 255);
    EXPECT_EQ(span[1], 255);
    EXPECT_EQ(span[2], 255);
    EXPECT_EQ(span[3], 255);
}

// For V6 Address
TEST(Address6, Parse) {
    EXPECT_EQ(IPAddress6::fromString("::1"), IPAddress6::loopback());
}

// For V4 / 6 Address
TEST(Address, Parse) {
    EXPECT_EQ(IPAddress::fromString("0.0.0.0").family(), AF_INET);
    EXPECT_EQ(IPAddress::fromString("255.255.255.255").family(), AF_INET);
    EXPECT_EQ(IPAddress::fromString("127.0.0.1").family(), AF_INET);
    
    EXPECT_EQ(IPAddress::fromString("::1").family(), AF_INET6);
    EXPECT_EQ(IPAddress::fromString("::").family(), AF_INET6);
    EXPECT_EQ(IPAddress::fromString("::ffff:192.168.1.1").family(), AF_INET6);
}

TEST(Address, ToString) {
    EXPECT_EQ(IPAddress(IPAddress4::any()).toString(), "0.0.0.0");
    EXPECT_EQ(IPAddress(IPAddress4::none()).toString(), "255.255.255.255");
}

TEST(AddrInfo, Get) {
    auto res = AddressInfo::fromHostname("www.baidu.com");
    if (res) {
        for (auto& addr : res.value().addresses()) {
            std::cout << addr.toString() << std::endl;
        }
    }
}

auto main() -> int {
    SockInitializer init;
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}