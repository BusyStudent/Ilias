#include <ilias/sync/mpmc.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>
#include <set>
#include <vector>

using namespace ilias;
using namespace std::literals;

// MARK: Basic

ILIAS_TEST(Mpmc, Basic) {
    auto [sender, receiver] = mpmc::channel<int>(10);
    EXPECT_TRUE(co_await sender.send(42));
    EXPECT_EQ(co_await receiver.recv(), 42);
}

ILIAS_TEST(Mpmc, Capacity) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    EXPECT_EQ(sender.capacity(), 1);
    EXPECT_EQ(receiver.capacity(), 1);
    EXPECT_TRUE(co_await sender.send(7));
    EXPECT_EQ(sender.trySend(8).error().reason, mpmc::TrySendError::Full);
    EXPECT_EQ(co_await receiver.recv(), 7);
    EXPECT_TRUE(co_await sender.send(8));
    EXPECT_EQ(co_await receiver.recv(), 8);
}

ILIAS_TEST(Mpmc, MoveOnly) {
    auto [sender, receiver] = mpmc::channel<std::unique_ptr<int> >(10);
    EXPECT_TRUE(co_await sender.send(std::make_unique<int>(42)));
    auto res = co_await receiver.recv();
    EXPECT_TRUE(res);
    EXPECT_EQ(**res, 42);
}

// MARK: Close

ILIAS_TEST(Mpmc, CloseSender) {
    auto [sender, receiver] = mpmc::channel<int>(10);
    sender.close();
    EXPECT_FALSE(co_await receiver.recv());
    EXPECT_TRUE(sender.isClosed());
    EXPECT_TRUE(receiver.isClosed());
}

ILIAS_TEST(Mpmc, CloseReceiver) {
    auto [sender, receiver] = mpmc::channel<int>(10);
    receiver.close();
    EXPECT_FALSE(co_await sender.send(42));
    EXPECT_TRUE(sender.isClosed());
    EXPECT_TRUE(receiver.isClosed());
}

ILIAS_TEST(Mpmc, SendOnClosedReturnsItem) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    receiver.close();
    auto res = co_await sender.send(42);
    EXPECT_FALSE(res);
    EXPECT_EQ(res.error(), 42);
}

ILIAS_TEST(Mpmc, BlockingSendOnClosedReturnsItem) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    receiver.close();
    auto res = sender.blockingSend(42);
    EXPECT_FALSE(res);
    EXPECT_EQ(res.error(), 42);
    co_return;
}

// MARK: Try

ILIAS_TEST(Mpmc, TryRecv) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    EXPECT_EQ(receiver.tryRecv(), Err(mpmc::TryRecvError::Empty));

    EXPECT_TRUE(co_await sender.send(42));
    EXPECT_EQ(receiver.tryRecv(), 42);

    sender.close();
    EXPECT_EQ(receiver.tryRecv(), Err(mpmc::TryRecvError::Closed));
}

ILIAS_TEST(Mpmc, TryRecvWithDataAfterSenderClosed) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    EXPECT_TRUE(co_await sender.send(42));
    sender.close();
    EXPECT_EQ(receiver.tryRecv(), 42);
}

ILIAS_TEST(Mpmc, TrySend) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    EXPECT_TRUE(sender.trySend(42));
    EXPECT_EQ(sender.trySend(42).error().reason, mpmc::TrySendError::Full);

    EXPECT_EQ(co_await receiver.recv(), 42);
    receiver.close();

    EXPECT_EQ(sender.trySend(42).error().reason, mpmc::TrySendError::Closed);
}

// MARK: Permit

ILIAS_TEST(Mpmc, PermitBasic) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    auto permit = sender.tryReserve();
    EXPECT_TRUE(permit);
    permit->send(42);

    EXPECT_EQ(sender.tryReserve(), Err(mpmc::TrySendError::Full));
    EXPECT_EQ(co_await receiver.recv(), 42);

    EXPECT_TRUE(co_await sender.reserve());
    receiver.close();
    EXPECT_EQ(sender.tryReserve(), Err(mpmc::TrySendError::Closed));
}

ILIAS_TEST(Mpmc, PermitDropReturnsSlot) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    {
        auto permit = sender.tryReserve();
        EXPECT_TRUE(permit);
        EXPECT_EQ(sender.trySend(1).error().reason, mpmc::TrySendError::Full);
    }
    EXPECT_TRUE(sender.trySend(1));
    EXPECT_EQ(co_await receiver.recv(), 1);
}

ILIAS_TEST(Mpmc, PermitOccupiesCapacity) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    auto permit = sender.tryReserve();
    EXPECT_TRUE(permit);
    EXPECT_EQ(sender.trySend(99).error().reason, mpmc::TrySendError::Full);
    permit->send(42);
    EXPECT_EQ(co_await receiver.recv(), 42);
}

ILIAS_TEST(Mpmc, ReserveOnClosed) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    receiver.close();
    auto permit = co_await sender.reserve();
    EXPECT_FALSE(permit);
}

ILIAS_TEST(Mpmc, ReserveBlocksUntilSpace) {
    auto [sender, receiver] = mpmc::channel<int>(1);
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

ILIAS_TEST(Mpmc, SendBlocksWhenFull) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    EXPECT_TRUE(co_await sender.send(1));
    auto handle = spawn([sender]() -> Task<void> {
        EXPECT_TRUE(co_await sender.send(2));
    });
    co_await this_coro::yield();
    EXPECT_EQ(co_await receiver.recv(), 1);
    EXPECT_EQ(co_await receiver.recv(), 2);
    EXPECT_TRUE(co_await std::move(handle));
}

ILIAS_TEST(Mpmc, RecvBlocksWhenEmpty) {
    auto [sender, receiver] = mpmc::channel<int>(1);
    auto handle = spawn([receiver]() mutable -> Task<void> {
        EXPECT_EQ(co_await receiver.recv(), 42);
    });
    co_await this_coro::yield();
    EXPECT_TRUE(co_await sender.send(42));
    EXPECT_TRUE(co_await std::move(handle));
}

// MARK: Cancel

ILIAS_TEST(Mpmc, CancelRecv) {
    auto [sender, receiver] = mpmc::channel<int>(10);
    auto handle = spawn([receiver]() mutable -> Task<void> {
        co_await receiver.recv();
        ILIAS_TRAP();
    });
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));
}

ILIAS_TEST(Mpmc, CancelSend) {
    auto [sender, receiver] = mpmc::channel<int>(1);
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

ILIAS_TEST(Mpmc, CancelReserve) {
    auto [sender, receiver] = mpmc::channel<int>(1);
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

// MARK: Lifetime

ILIAS_TEST(Mpmc, MultiSenderLifetime) {
    auto [sender, receiver] = mpmc::channel<int>(4);
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

ILIAS_TEST(Mpmc, MultiReceiverLifetime) {
    auto [sender, receiver] = mpmc::channel<int>(4);
    auto receiver2 = receiver;
    EXPECT_GE(receiver.useCount(), 2);
    receiver2.close();
    EXPECT_FALSE(sender.isClosed());
    EXPECT_TRUE(co_await sender.send(1));
    EXPECT_EQ(co_await receiver.recv(), 1);
    receiver.close();
    EXPECT_TRUE(sender.isClosed());
    EXPECT_FALSE(co_await sender.send(2));
}

ILIAS_TEST(Mpmc, ReceiverCopyable) {
    auto [sender, receiver] = mpmc::channel<int>(10);
    auto receiver2 = receiver;
    EXPECT_GE(receiver.useCount(), 2);
    EXPECT_TRUE(co_await sender.send(1));
    EXPECT_TRUE(co_await sender.send(2));
    EXPECT_EQ(co_await receiver.recv(), 1);
    EXPECT_EQ(co_await receiver2.recv(), 2);
    receiver2.close();
    EXPECT_FALSE(sender.isClosed());
    receiver.close();
    EXPECT_TRUE(sender.isClosed());
}

// MARK: Multi producer / multi consumer

ILIAS_TEST(Mpmc, MultiProducer) {
    auto [sender, receiver] = mpmc::channel<int>(10);
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

ILIAS_TEST(Mpmc, MultiConsumer) {
    auto [sender, receiver] = mpmc::channel<int>(10);
    auto recvWorker = [](auto receiver) -> Task<std::vector<int> > {
        std::vector<int> values;
        while (auto value = co_await receiver.recv()) {
            values.push_back(*value);
        }
        co_return values;
    };
    auto group = TaskGroup<std::vector<int> >();
    for (int i = 0; i < 4; i++) {
        group.spawn(recvWorker(receiver));
    }
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(co_await sender.send(i));
    }
    sender.close();
    auto results = co_await group.waitAll();
    std::set<int> got;
    auto total = 0;
    auto active = 0;
    for (auto &values : results) {
        total += static_cast<int>(values.size());
        if (!values.empty()) {
            ++active;
        }
        for (auto v : values) {
            EXPECT_TRUE(got.insert(v).second);
        }
    }
    EXPECT_EQ(total, 100);
    EXPECT_EQ(got.size(), 100);
    EXPECT_EQ(*got.begin(), 0);
    EXPECT_EQ(*got.rbegin(), 99);
    EXPECT_GE(active, 1);
}

ILIAS_TEST(Mpmc, MultiProducerMultiConsumer) {
    auto [sender, receiver] = mpmc::channel<int>(16);
    auto sendWorker = [](auto sender, int base) -> Task<void> {
        for (int i = 0; i < 25; i++) {
            EXPECT_TRUE(co_await sender.send(base + i));
        }
    };
    auto recvWorker = [](auto receiver) -> Task<std::vector<int> > {
        std::vector<int> values;
        while (auto value = co_await receiver.recv()) {
            values.push_back(*value);
        }
        co_return values;
    };
    auto sendGroup = TaskGroup<void>();
    auto recvGroup = TaskGroup<std::vector<int> >();
    for (int i = 0; i < 4; i++) {
        sendGroup.spawn(sendWorker(sender, i * 25));
        recvGroup.spawn(recvWorker(receiver));
    }
    auto _ = co_await sendGroup.waitAll();
    sender.close();
    auto results = co_await recvGroup.waitAll();
    std::set<int> got;
    auto total = 0;
    for (auto &values : results) {
        total += static_cast<int>(values.size());
        for (auto v : values) {
            EXPECT_TRUE(got.insert(v).second);
        }
    }
    EXPECT_EQ(total, 100);
    EXPECT_EQ(got.size(), 100);
    EXPECT_EQ(*got.begin(), 0);
    EXPECT_EQ(*got.rbegin(), 99);
}

ILIAS_TEST(Mpmc, CloseWakesAllReceivers) {
    auto [sender, receiver] = mpmc::channel<int>(4);
    auto recvWorker = [](auto receiver) -> Task<bool> {
        auto value = co_await receiver.recv();
        co_return !value; // true if closed with no value
    };
    auto group = TaskGroup<bool>();
    for (int i = 0; i < 4; i++) {
        group.spawn(recvWorker(receiver));
    }
    co_await this_coro::yield();
    sender.close();
    auto results = co_await group.waitAll();
    EXPECT_EQ(results.size(), 4);
    for (auto closed : results) {
        EXPECT_TRUE(closed);
    }
}

ILIAS_TEST(Mpmc, ProducerConsumerPipeline) {
    auto [sender, receiver] = mpmc::channel<int>(10);
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

ILIAS_TEST(Mpmc, BlockingSendCrossThread) {
    auto [sender, receiver] = mpmc::channel<int>(10);
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

ILIAS_TEST(Mpmc, CrossThreadMultiConsumer) {
    auto [sender, receiver] = mpmc::channel<int>(10);
    auto exec = useExecutor<EventLoop>();
    auto thread1 = Thread(exec, [sender]() mutable -> Task<void> {
        for (int i = 0; i < 100; i++) {
            EXPECT_TRUE(co_await sender.send(i));
        }
        sender.close();
    });
    std::atomic<int> received {0};
    auto thread2 = std::thread([receiver, &received]() mutable {
        while (auto value = receiver.blockingRecv()) {
            received.fetch_add(1, std::memory_order_relaxed);
        }
    });
    auto thread3 = std::thread([receiver = std::move(receiver), &received]() mutable {
        while (auto value = receiver.blockingRecv()) {
            received.fetch_add(1, std::memory_order_relaxed);
        }
    });
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(co_await sender.send(i));
    }
    sender.close();

    co_await thread1.join();
    thread2.join();
    thread3.join();
    EXPECT_EQ(received.load(), 200);
}
