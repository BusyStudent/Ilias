#include <ilias/net/msghdr.hpp>
#include <ilias/net/cmsg.hpp>
#include <ilias/net/udp.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>

using namespace ilias;

TEST(Cmsg, Build) {
    CmsgSpace space;
    
    // Build the message
    auto control = CmsgWriter{space}
        .end();
}

ILIAS_RTEST(Cmsg, Read) {
    using namespace ilias::literals;

    ILIAS_CO_TRY(auto sender, co_await UdpSocket::bind("127.0.0.1:0"));
    ILIAS_CO_TRY(auto receiver, co_await UdpSocket::bind("127.0.0.1:0"));
    ILIAS_CO_TRY(auto target, receiver.localEndpoint());

    IoVec payload = "Hello, world!"_bin;
    {
        MsgHdr msg;
        CmsgSpace space;
        msg.setEndpoint(target);
        msg.setBuffers(std::span{&payload, 1});
        // msg.setControl(CmsgWriter{space}
        //     .append(cmsg::Ipv4Ttl{128})
        //     .end()
        // );
        auto n = co_await sender.sendmsg(msg);
        EXPECT_EQ(n, payload.size());
    }
    {
        std::byte storage[128];
        MutableIoVec buffer{storage};
        MutableMsgHdr msg;
        CmsgSpace space;
        space.resize(256);

        msg.setBuffers(std::span{&buffer, 1});
        msg.setControl(space.data());
        ILIAS_CO_TRY(auto n, co_await receiver.recvmsg(msg));
        EXPECT_EQ(n, payload.size());

        bool got = false;
        for (const auto &msg : CmsgReader{msg}) {
            // if (auto ttl = msg.decode<cmsg::Ipv4Ttl>()) {
            //     EXPECT_EQ(ttl->value(), 128);
            // }
        }
    }
    co_return {};
}