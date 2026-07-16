#include <ilias/sync/mpsc.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>
#include <set>

using namespace ilias;
using namespace std::literals;

// MARK: Basic

ILIAS_TEST(Mpsc, Basic) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    EXPECT_TRUE(co_await sender.send(42));
    EXPECT_EQ(co_await receiver.recv(), 42);
}

ILIAS_TEST(Mpsc, Capacity) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_EQ(sender.capacity(), 1);
    EXPECT_TRUE(co_await sender.send(7));
    EXPECT_EQ(sender.trySend(8).error().reason, mpsc::TrySendError::Full);
    EXPECT_EQ(co_await receiver.recv(), 7);
    EXPECT_TRUE(co_await sender.send(8));
    EXPECT_EQ(co_await receiver.recv(), 8);
}

ILIAS_TEST(Mpsc, MoveOnly) {
    auto [sender, receiver] = mpsc::channel<std::unique_ptr<int> >(10);
    EXPECT_TRUE(co_await sender.send(std::make_unique<int>(42)));
    auto res = co_await receiver.recv();
    EXPECT_TRUE(res);
    EXPECT_EQ(**res, 42);
}

// MARK: Close

ILIAS_TEST(Mpsc, CloseSender) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    sender.close();
    EXPECT_FALSE(co_await receiver.recv());
    EXPECT_TRUE(sender.isClosed());
    EXPECT_TRUE(receiver.isClosed());
}

ILIAS_TEST(Mpsc, CloseReceiver) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    receiver.close();
    EXPECT_FALSE(co_await sender.send(42));
    EXPECT_TRUE(sender.isClosed());
    EXPECT_TRUE(receiver.isClosed());
}

ILIAS_TEST(Mpsc, SendOnClosedReturnsItem) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    receiver.close();
    auto res = co_await sender.send(42);
    EXPECT_FALSE(res);
    EXPECT_EQ(res.error(), 42);
}

ILIAS_TEST(Mpsc, BlockingSendOnClosedReturnsItem) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    receiver.close();
    auto res = sender.blockingSend(42);
    EXPECT_FALSE(res);
    EXPECT_EQ(res.error(), 42);
    co_return;
}

// MARK: Try

ILIAS_TEST(Mpsc, TryRecv) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_EQ(receiver.tryRecv(), Err(mpsc::TryRecvError::Empty));

    EXPECT_TRUE(co_await sender.send(42));
    EXPECT_EQ(receiver.tryRecv(), 42);

    sender.close();
    EXPECT_EQ(receiver.tryRecv(), Err(mpsc::TryRecvError::Closed));
}

ILIAS_TEST(Mpsc, TryRecvWithDataAfterSenderClosed) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_TRUE(co_await sender.send(42));
    sender.close();
    EXPECT_EQ(receiver.tryRecv(), 42);
}

ILIAS_TEST(Mpsc, TrySend) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_TRUE(sender.trySend(42));
    EXPECT_EQ(sender.trySend(42).error().reason, mpsc::TrySendError::Full);

    EXPECT_EQ(co_await receiver.recv(), 42);
    receiver.close();

    EXPECT_EQ(sender.trySend(42).error().reason, mpsc::TrySendError::Closed);
}

// MARK: Permit

ILIAS_TEST(Mpsc, PermitBasic) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    auto permit = sender.tryReserve();
    EXPECT_TRUE(permit);
    permit->send(42);

    EXPECT_EQ(sender.tryReserve(), Err(mpsc::TrySendError::Full));
    EXPECT_EQ(co_await receiver.recv(), 42);

    EXPECT_TRUE(co_await sender.reserve());
    receiver.close();
    EXPECT_EQ(sender.tryReserve(), Err(mpsc::TrySendError::Closed));
}

ILIAS_TEST(Mpsc, PermitDropReturnsSlot) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    {
        auto permit = sender.tryReserve();
        EXPECT_TRUE(permit);
        EXPECT_EQ(sender.trySend(1).error().reason, mpsc::TrySendError::Full);
    }
    EXPECT_TRUE(sender.trySend(1));
    EXPECT_EQ(co_await receiver.recv(), 1);
}

ILIAS_TEST(Mpsc, PermitOccupiesCapacity) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    auto permit = sender.tryReserve();
    EXPECT_TRUE(permit);
    EXPECT_EQ(sender.trySend(99).error().reason, mpsc::TrySendError::Full);
    permit->send(42);
    EXPECT_EQ(co_await receiver.recv(), 42);
}

ILIAS_TEST(Mpsc, ReserveOnClosed) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    receiver.close();
    auto permit = co_await sender.reserve();
    EXPECT_FALSE(permit);
}

ILIAS_TEST(Mpsc, ReserveBlocksUntilSpace) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_TRUE(co_await sender.send(1));
    auto handle = spawn([sender]() -> Task<void> {
        auto permit = co_await sender.reserve();
        EXPECT_TRUE(permit);
        permit->send(2);
    });
    co_await this_coro::yield();
    EXPECT_EQ(co_await receiver.recv(), 1);
    EXPECT_EQ(co_await receiver.recv(), 2);
    EXPECT_TRUE(co_await std::move(handle));
}

// MARK: Backpressure

ILIAS_TEST(Mpsc, SendBlocksWhenFull) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_TRUE(co_await sender.send(1));
    auto handle = spawn([sender]() -> Task<void> {
        EXPECT_TRUE(co_await sender.send(2));
    });
    co_await this_coro::yield();
    EXPECT_EQ(co_await receiver.recv(), 1);
    EXPECT_EQ(co_await receiver.recv(), 2);
    EXPECT_TRUE(co_await std::move(handle));
}

ILIAS_TEST(Mpsc, RecvBlocksWhenEmpty) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    auto handle = spawn([receiver = std::move(receiver)]() mutable -> Task<void> {
        EXPECT_EQ(co_await receiver.recv(), 42);
    });
    co_await this_coro::yield();
    EXPECT_TRUE(co_await sender.send(42));
    EXPECT_TRUE(co_await std::move(handle));
}

// MARK: Cancel

ILIAS_TEST(Mpsc, CancelRecv) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    auto handle = spawn([receiver = std::move(receiver)]() mutable -> Task<void> {
        co_await receiver.recv();
        ILIAS_TRAP();
    });
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));
}

ILIAS_TEST(Mpsc, CancelSend) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_TRUE(co_await sender.send(1));
    auto handle = spawn([sender]() -> Task<void> {
        auto _ = co_await sender.send(2); // cancelled item is lost
        ILIAS_TRAP();
    });
    co_await this_coro::yield();
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));
    EXPECT_EQ(co_await receiver.recv(), 1);
    EXPECT_TRUE(co_await sender.send(3));
    EXPECT_EQ(co_await receiver.recv(), 3);
}

ILIAS_TEST(Mpsc, CancelReserve) {
    auto [sender, receiver] = mpsc::channel<int>(1);
    EXPECT_TRUE(co_await sender.send(1));
    auto handle = spawn([sender]() -> Task<void> {
        auto _ = co_await sender.reserve();
        ILIAS_TRAP();
    });
    co_await this_coro::yield();
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));
    EXPECT_EQ(co_await receiver.recv(), 1);
    auto permit = co_await sender.reserve();
    EXPECT_TRUE(permit);
    permit->send(2);
    EXPECT_EQ(co_await receiver.recv(), 2);
}

// MARK: Lifetime / multi producer

ILIAS_TEST(Mpsc, MultiSenderLifetime) {
    auto [sender, receiver] = mpsc::channel<int>(4);
    auto sender2 = sender;
    EXPECT_GE(sender.useCount(), 2);
    sender2.close();
    EXPECT_FALSE(sender.isClosed());
    EXPECT_FALSE(receiver.isClosed());
    EXPECT_TRUE(co_await sender.send(1));
    EXPECT_EQ(co_await receiver.recv(), 1);
    sender.close();
    EXPECT_TRUE(receiver.isClosed());
    EXPECT_FALSE(co_await receiver.recv());
}

ILIAS_TEST(Mpsc, MultiProducer) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    auto sendWorker = [](auto sender) -> Task<void> {
        for (int i = 0; i < 10; i++) {
            EXPECT_TRUE(co_await sender.send(i));
        }
    };
    auto group = TaskGroup<void>();
    for (int i = 0; i < 10; i++) {
        group.spawn(sendWorker(sender));
    }
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(co_await receiver.recv());
    }
    auto _ = co_await group.waitAll();
}

ILIAS_TEST(Mpsc, MultiProducerContent) {
    auto [sender, receiver] = mpsc::channel<int>(8);
    auto sendWorker = [](auto sender, int base) -> Task<void> {
        for (int i = 0; i < 25; i++) {
            EXPECT_TRUE(co_await sender.send(base + i));
        }
    };
    auto group = TaskGroup<void>();
    for (int i = 0; i < 4; i++) {
        group.spawn(sendWorker(sender, i * 25));
    }
    std::set<int> got;
    for (int i = 0; i < 100; i++) {
        auto value = co_await receiver.recv();
        EXPECT_TRUE(value);
        got.insert(*value);
    }
    auto _ = co_await group.waitAll();
    EXPECT_EQ(got.size(), 100);
    EXPECT_EQ(*got.begin(), 0);
    EXPECT_EQ(*got.rbegin(), 99);
}

ILIAS_TEST(Mpsc, ProducerConsumerPipeline) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    auto handle = spawn([receiver = std::move(receiver)]() mutable -> Task<void> {
        for (int i = 0; i < 100; i++) {
            EXPECT_EQ(co_await receiver.recv(), i);
        }
        receiver.close();
    });
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(co_await sender.send(i));
    }
    co_await this_coro::yield();
    EXPECT_FALSE(co_await sender.send(100));
    EXPECT_FALSE(co_await sender.send(101));
    EXPECT_TRUE(co_await std::move(handle));
}

// MARK: Cross thread

ILIAS_TEST(Mpsc, BlockingSendCrossThread) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    auto thread = std::thread([sender]() mutable {
        for (int i = 0; i < 100; i++) {
            EXPECT_TRUE(sender.blockingSend(i));
        }
    });
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(co_await receiver.recv(), i);
    }
    receiver.close();
    thread.join();
}

ILIAS_TEST(Mpsc, CrossThread) {
    auto [sender, receiver] = mpsc::channel<int>(10);
    auto exec = useExecutor<EventLoop>();
    auto thread1 = Thread(exec, [sender]() mutable -> Task<void> {
        for (int i = 0; i < 100; i++) {
            EXPECT_TRUE(co_await sender.send(i));
        }
        sender.close();
    });
    auto thread2 = std::thread([receiver = std::move(receiver)]() mutable {
        for (int i = 0; i < 200; i++) {
            EXPECT_TRUE(receiver.blockingRecv());
        }
        EXPECT_FALSE(receiver.blockingRecv());
    });
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(co_await sender.send(i));
    }
    sender.close();

    co_await thread1.join();
    thread2.join();
}
