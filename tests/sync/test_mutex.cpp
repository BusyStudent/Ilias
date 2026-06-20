#include <ilias/sync/mutex.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>

using namespace ilias;
using namespace std::literals;

// Mutex
ILIAS_TEST(Sync, BasicMutexLockUnlock) {
    Mutex mtx;
    EXPECT_FALSE(mtx.isLocked());

    auto lock = co_await mtx.lock();
    EXPECT_TRUE(mtx.isLocked());

    lock.unlock();
    EXPECT_FALSE(mtx.isLocked());
}

ILIAS_TEST(Sync, MutexMultipleWaiters) {
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

ILIAS_TEST(Sync, MutexCancel) {
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

ILIAS_TEST(Sync, MutexCrossThread) {
    Mutex mtx;
    int value = 0;

    auto callable = [&]() -> Thread<void> {
        for (int i = 0; i < 100000; ++i) {
            auto lock = co_await mtx.lock();
            value += 1;
        }
    };
    auto callable2 = [&]() -> void {
        for (int i = 0; i < 100000; ++i) {
            auto lock = mtx.blockingLock();
            value += 1;
        }
    };

    auto thread = callable();
    auto thread2 = std::thread(callable2);
    
    for (int i = 0; i < 100000; ++i) {
        auto lock = co_await mtx.lock();
        value += 1;
    }

    co_await thread.join();
    thread2.join();
    EXPECT_EQ(value, 300000);
}

// Locked
ILIAS_TEST(Sync, Locked) {
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