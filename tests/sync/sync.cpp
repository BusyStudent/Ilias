#include <ilias/sync/mutex.hpp>
#include <ilias/sync/event.hpp>
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

auto main(int argc, char **argv) -> int {
    runtime::EventLoop loop;
    loop.install();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}