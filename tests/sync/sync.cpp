#include <ilias/sync/mutex.hpp>
#include <ilias/sync/event.hpp>
#include <ilias/sync/semaphore.hpp>
#include <ilias/sync/oneshot.hpp>
#include <ilias/sync/mpsc.hpp>
#include <ilias/task.hpp>
#include "testing.hpp"

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

// Mutex
CORO_TEST(Sync, BasicMutexLockUnlock) {
    Mutex mtx;
    EXPECT_FALSE(mtx.isLocked());

    auto lock = co_await mtx.lock();
    EXPECT_TRUE(mtx.isLocked());

    lock.unlock();
    EXPECT_FALSE(mtx.isLocked());
}

CORO_TEST(Sync, MutexMultipleWaiters) {
    Mutex mtx;
    int shared = 0;

    auto taskA = [&]() -> Task<int> {
        auto lock = co_await mtx.lock();
        shared += 1;
        co_await sleep(10ms);
        co_return shared;
    };

    auto taskB = [&]() -> Task<int> {
        auto lock = co_await mtx.lock();
        shared += 2;
        co_return shared;
    };

    auto taskC = [&]() -> Task<int> {
        co_await sleep(20ms);
        auto lock = co_await mtx.lock();
        shared += 3;
        co_return shared;
    };

    auto [a, b, c] = co_await whenAll(taskA(), taskB(), taskC());
    EXPECT_EQ(shared, 6);
}

CORO_TEST(Sync, MutexCancel) {
    Mutex mtx;

    auto lock = co_await mtx.lock();
    auto taskA = [&]() -> Task<void> {
        auto _ = co_await mtx.lock();
        ILIAS_TRAP(); // never reached
    };
    auto handle = spawn(taskA());
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));
}

// Locked
CORO_TEST(Sync, Locked) {
    Locked<int> value {10};
    EXPECT_FALSE(value.isLocked());
    {
        auto ptr = value.tryLock();
        EXPECT_TRUE(ptr);
        EXPECT_TRUE(value.isLocked());
        EXPECT_EQ(*ptr->get(), 10);
        **ptr = 114514;
    }
    EXPECT_FALSE(value.isLocked());
    {
        auto ptr = co_await value;
        EXPECT_EQ(*ptr, 114514);
    }
    co_return;
}

CORO_TEST(Sync, Event) {
    Event event;
    
    EXPECT_FALSE(event.isSet());
    event.set();
    EXPECT_TRUE(event.isSet());

    co_await event;

    // Try wait on it
    event.clear();
    auto fn = [&]() -> Task<void> {
        co_await event;  
    };
    auto handle = spawn(fn());
    co_await sleep(10ms);
    event.set(); // Wake up the task
    
    EXPECT_TRUE(co_await std::move(handle));
}

CORO_TEST(Sync, Semaphore) {
    Semaphore sem(10);
    auto premit = co_await sem.acquire();
    EXPECT_EQ(sem.available(), 9);
    auto premit2 = co_await sem.acquire();
    EXPECT_EQ(sem.available(), 8);

    auto group = TaskGroup<void>();
    for (int i = 0; i < 100; ++i) {
        group.spawn([&]() -> Task<void> {
            auto _ = co_await sem.acquire();
            co_await sleep(10ms);
        });
    }
    auto result = co_await group.waitAll();
    EXPECT_EQ(result.size(), 100);
    EXPECT_EQ(sem.available(), 8);
}

CORO_TEST(Sync, Oneshot) {
    {
        auto [sender, receiver] = oneshot::channel<int>();
        EXPECT_TRUE(sender.send(42));
        EXPECT_EQ(co_await std::move(receiver), 42);
    }
    { // close
        auto [sender, receiver] = oneshot::channel<int>();
        sender.close();
        EXPECT_FALSE(co_await std::move(receiver));
    }
    { // blocking
        auto [sender, receiver] = oneshot::channel<int>();
        auto recv = [&]() -> Task<std::optional<int> > {
            co_return co_await std::move(receiver);  
        };
        auto send = [&]() -> Task<void> {
            EXPECT_TRUE(sender.send(42));            
            co_return;
        };
        auto [a, b] = co_await whenAll(recv(), send());
        EXPECT_EQ(a, 42);
    }
    { // blocking close
        auto [sender, receiver] = oneshot::channel<int>();
        auto recv = [&]() -> Task<std::optional<int> > {
            co_return co_await std::move(receiver);  
        };
        auto send = [&]() -> Task<void> {
            sender.close();
            co_return;
        };
        auto [a, b] = co_await whenAll(recv(), send());
        EXPECT_EQ(a, std::nullopt);
    }
    {
        // cancel
        auto [sender, receiver] = oneshot::channel<int>();
        auto recv = [&]() -> Task<std::optional<int> > {
            co_return co_await std::move(receiver);  
        };
        auto handle = spawn(recv());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
}

CORO_TEST(Sync, Mpsc) {
    {
        auto [sender, receiver] = mpsc::channel<int>(10);
        EXPECT_TRUE(co_await sender.send(42));
        EXPECT_EQ(co_await receiver.recv(), 42);
    }
    { // close
        auto [sender, receiver] = mpsc::channel<int>(10);
        sender.close();
        EXPECT_FALSE(co_await receiver.recv());
        EXPECT_TRUE(sender.isClosed());
        EXPECT_TRUE(receiver.isClosed());
    }
    {
        auto [sender, receiver] = mpsc::channel<int>(10);
        receiver.close();
        EXPECT_FALSE(co_await sender.send(42));
        EXPECT_TRUE(sender.isClosed());
        EXPECT_TRUE(receiver.isClosed());
    }
    { // blocking
        auto [sender, receiver] = mpsc::channel<int>(10);
        auto recvWorker = [](auto receiver) -> Task<void> {
            for (int i = 0; i < 100; i++) {
                EXPECT_EQ(co_await receiver.recv(), i);
            }  
            receiver.close();
        };
        auto handle = spawn(recvWorker(std::move(receiver)));
        for (int i = 0; i < 100; i++) {
            EXPECT_TRUE(co_await sender.send(i));
        }
        co_await this_coro::yield();
        EXPECT_FALSE(co_await sender.send(100)); // closed
        EXPECT_FALSE(co_await sender.send(101)); // closed
        EXPECT_TRUE(co_await std::move(handle)); // wait for the recvWorker to finish
    }
    {
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
    {
        // cancel
        auto [sender, receiver] = mpsc::channel<int>(10);
        auto recv = [&]() -> Task<void> {
            co_await receiver.recv();
            ILIAS_TRAP(); // should not reach here
        };
        auto handle = spawn(recv());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
}

auto main(int argc, char **argv) -> int {
    EventLoop loop;
    loop.install();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}