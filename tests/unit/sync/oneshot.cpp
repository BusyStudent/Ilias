#include <ilias/task/task.hpp>
#include <ilias/task/mini_executor.hpp>
#include <ilias/task/when_all.hpp>
#include <ilias/sync/oneshot.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(Oneshot, Basic) {
    auto [sender, receiver] = oneshot::channel<int>();

    ASSERT_TRUE(!receiver.tryRecv());
    sender.send(42);
    ASSERT_TRUE(receiver.tryRecv().value() == 42);
}

TEST(Oneshot, SenderClosed) {
    auto [sender, receiver] = oneshot::channel<int>();
    sender.close();

    ASSERT_TRUE(receiver.tryRecv().error() == Error::ChannelBroken);
}

TEST(Oneshot, ReceiverClosed) {
    auto [sender, receiver] = oneshot::channel<int>();
    receiver.close();

    ASSERT_TRUE(sender.send(42).error() == Error::ChannelBroken);
}

TEST(Oneshot, Async) {
    auto [sender, receiver] = oneshot::channel<int>();

    auto task1 = [&]() -> Task<int> {
        co_return co_await receiver;
    };
    auto task2 = [&]() -> Task<void> {
        sender.send(114514);
        co_return {};
    };
    auto [result1, result2] = whenAll(task1(), task2()).wait();
    ASSERT_TRUE(result1);
    ASSERT_TRUE(result2);

    ASSERT_EQ(result1.value(), 114514);
}

TEST(Oneshot, AsyncSenderClosed) {
    auto [sender, receiver] = oneshot::channel<int>();

    auto task1 = [&]() -> Task<int> {
        co_return co_await receiver;
    };
    auto task2 = [&]() -> Task<void> {
        sender.close();
        co_return {};
    };
    auto [result1, result2] = whenAll(task1(), task2()).wait();
    ASSERT_TRUE(result1.error() == Error::ChannelBroken);
    ASSERT_TRUE(result2);
}

TEST(Oneshot, AsyncReceiverClosed) {
    auto [sender, receiver] = oneshot::channel<int>();

    auto task1 = [&]() -> Task<int> {
        receiver.close();
        co_return {};
    };
    auto task2 = [&]() -> Task<void> {
        co_return sender.send(114514);
    };
    auto [result1, result2] = whenAll(task1(), task2()).wait();
    ASSERT_TRUE(result1);
    ASSERT_TRUE(result2.error() == Error::ChannelBroken);
}

auto main(int argc, char** argv) -> int {
    MiniExecutor exec;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}