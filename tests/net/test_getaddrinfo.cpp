#include <ilias/testing.hpp>
#include <ilias/net.hpp>
using namespace ilias;

ILIAS_TEST(Net, GetAddrInfo) {
    {
        auto info = co_await AddressInfo::fromHostname("www.baidu.com");
        EXPECT_TRUE(info);
    }
    {
        auto info = co_await AddressInfo::fromHostname("impossiblehostname.unknown");
        EXPECT_FALSE(info);
        EXPECT_EQ(info.error(), GaiError::NotFound);
        std::cout << info.error().message() << std::endl;
    }
    {
        auto info = co_await AddressInfo::lookup("www.baidu.com:80");
        EXPECT_TRUE(info);
        for (auto endpoint : info.value()) {
            std::cout << endpoint.toString() << std::endl;
        }
    }
}
