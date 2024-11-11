#include <ilias/sync/mpsc.hpp>
#include <ilias/sync/mpmc.hpp>
#include <ilias/task.hpp>
#include <gtest/gtest.h>
#include <chrono>

using namespace ILIAS_NAMESPACE;
using namespace std::chrono_literals;

auto sendForN(auto &sender, size_t n) -> Task<void> {
    for (size_t i = 0; i < n; ++i) {
        auto ret = co_await sender.send(i);
        if (!ret) co_return Unexpected(ret.error());
    }
    co_return {};
}

auto recvForN(auto &receiver, size_t n) -> Task<void> {
    for (size_t i = 0; i < n; ++i) {
        auto ret = co_await receiver.recv();
        if (!ret) co_return Unexpected(ret.error());
        ILIAS_ASSERT(*ret == i); //< Sender is guaranteed to send i
    }
    co_return {};
};

TEST(Mpsc, SendRecv) {
    auto sendAndRecv = [&](size_t capacity, size_t n) {
        auto [sender, receiver] = mpsc::channel<int>(capacity);
        return whenAll(
            sendForN(sender, n),
            recvForN(receiver, n)
        ).wait();
    };
    auto recvAndSend = [&](size_t capacity, size_t n) {
        auto [sender, receiver] = mpsc::channel<int>(capacity);
        return whenAll(
            recvForN(receiver, n),
            sendForN(sender, n)
        ).wait();
    };

    // sendAndRecv
    {
        auto [send, recv] = sendAndRecv(100, 50);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }

    {
        auto [send, recv] = sendAndRecv(1, 2);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }

    {
        auto [send, recv] = sendAndRecv(1, 1);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }

    {
        auto [send, recv] = sendAndRecv(10, 100);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }

    // recvAndSend
    {
        auto [recv, send] = recvAndSend(100, 50);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }

    {
        auto [recv, send] = recvAndSend(1, 2);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }

    {
        auto [recv, send] = recvAndSend(1, 1);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }

    {
        auto [recv, send] = recvAndSend(10, 100);
        EXPECT_TRUE(send);
        EXPECT_TRUE(recv);
    }
}

TEST(Mpsc, Close) { //< Test for recv and then close it 
    auto recv = [](mpsc::Receiver<int> &receiver) -> Task<void> {
        auto ret = co_await receiver.recv();
        if (!ret) co_return Unexpected(ret.error());
        co_return {};
    };
    auto close = [](auto &sender) -> Task<void> {
        sender.close();
        co_return {};
    };
    auto recvAndClose = [&]() {
        auto [sender, receiver] = mpsc::channel<int>(1);
        return whenAll(
            recv(receiver),
            close(sender)
        ).wait();
    };
    auto closeAndRecv = [&]() {
        auto [sender, receiver] = mpsc::channel<int>(1);
        return whenAll(
            close(sender),
            recv(receiver)
        ).wait();
    };

    {
        auto [recv, close] = recvAndClose();
        EXPECT_FALSE(recv); //< Error should be ChannelBroken
        EXPECT_EQ(recv.error(), Error::ChannelBroken);
        EXPECT_TRUE(close);
    }

    {
        auto [close, recv] = closeAndRecv();
        EXPECT_TRUE(close);
        EXPECT_FALSE(recv); //< Error should be ChannelBroken
        EXPECT_EQ(recv.error(), Error::ChannelBroken);
    }
}

TEST(Mpsc, Cancel) {
    {
        auto [sender, receiver] = mpsc::channel<int>(1);
        auto recvWithCancel = [](auto &receiver) -> Task<void> {
            auto doRecv = [&]() -> Task<int> {
                co_return co_await receiver.recv();
            };
            auto [recv, _] = co_await whenAny(doRecv(), sleep(1ms));
            co_return {};
        };
        recvWithCancel(receiver).wait();
    }

    {
        auto [sender, receiver] = mpsc::channel<int>(1);
        sender.trySend(1); //< Make it to be full
        auto sendWithCancel = [](auto &sender) -> Task<void> {
            auto doSend = [&]() -> Task<void> {
                co_return co_await sender.send(1);
            };
            auto [send, _] = co_await whenAny(doSend(), sleep(1ms));
            co_return {};
        };
        sendWithCancel(sender).wait();
    }
}

auto main(int argc, char** argv) -> int {
    testing::InitGoogleTest(&argc, argv);
    MiniExecutor exec;
    return RUN_ALL_TESTS();
}