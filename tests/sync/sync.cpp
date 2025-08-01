#include <ilias/sync/mutex.hpp>
#include <ilias/sync/event.hpp>
#include <ilias/sync/semaphore.hpp>
#include <ilias/sync/oneshot.hpp>
#include <ilias/task.hpp>
#include "testing.hpp"

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

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
        ::abort(); // never reached
    };
    auto handle = spawn(taskA());
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));
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
}

auto main(int argc, char **argv) -> int {
    runtime::EventLoop loop;
    loop.install();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}