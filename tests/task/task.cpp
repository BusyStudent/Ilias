#include <ilias/task/generator.hpp>
#include <ilias/task/when_all.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/task/utils.hpp>
#include <ilias/task/task.hpp>
#include <gtest/gtest.h>
#include "testing.hpp"

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

template <typename T>
auto returnInput(T val) -> Task<T> {
    co_return val;
}

template <typename T>
auto throwInput(T val) -> Task<void> {
    throw val;
    co_return;
}

auto testTask() -> Task<void> {
    EXPECT_EQ(co_await returnInput(42), 42);

    // Test exception handling
    try {
        co_await throwInput(42);
        ::abort();
    }
    catch (int val) {
        EXPECT_EQ(val, 42);
    }
    
    co_await sleep(10ms);
    co_await sleep(20ms);
    co_return;
}

auto range(int start, int end) -> Generator<int> {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

TEST(Task, DefaultConstructor) {
    testTask().wait();
}

TEST(Task, Spawn) {
    auto handle = spawn(testTask());
    auto val = std::move(handle).wait();
    EXPECT_TRUE(val.has_value());

    auto handle2 = spawn(testTask());
    handle2.stop();
    auto val2 = std::move(handle2).wait();
    EXPECT_FALSE(val2.has_value());
}

TEST(Task, SpawnCallable) {
    spawn([]() -> Task<void> {
        co_return;
    }).wait();

    spawn([i = 42]() -> Task<int> {
        assert(i == 42);
        co_return i;
    }).wait();
}

TEST(Task, SpawnAwait) {
    auto fn = []() -> Task<void> {
        co_await spawn(testTask());
    };
    fn().wait();
}

TEST(Task, SpawnBlocking) {
    auto val = spawnBlocking([]() -> int {
        return 42;
    }).wait().value();
    EXPECT_EQ(val, 42);

    EXPECT_THROW(spawnBlocking([]() {
        std::cout << "Hello Exception!" << std::endl;
        throw std::exception();
    }).wait(), std::exception);
}

CORO_TEST(Task, Generator) {
    ilias_for_await(int i, range(0, 10)) {
        EXPECT_TRUE(i >= 0 && i < 10);
    }
}

CORO_TEST(Task, WhenAll) {
    {
        auto [a, b] = co_await whenAll(returnInput(42), returnInput(43));
        EXPECT_EQ(a, 42);
        EXPECT_EQ(b, 43);
    }
    {
        auto [a, b] = co_await whenAll(returnInput(42), sleep(10ms));
        EXPECT_EQ(a, 42);
    }
    {
        auto [a, b] = co_await whenAll(sleep(10ms), returnInput(42));
        EXPECT_EQ(b, 42);
    }
    {
        auto [a, b] = co_await whenAll(sleep(10ms), sleep(20ms));
    }

    // Check stopping
    auto handle = spawn([]() -> Task<void> {
        auto _ = co_await whenAll(sleep(10ms), sleep(20ms));
        ::abort(); // should not be reached
    });
    handle.stop();
    EXPECT_TRUE(!co_await std::move(handle));
}

CORO_TEST(Task, WhenAny) {
    {
        auto [a, b] = co_await whenAny(returnInput(42), returnInput(43));
        EXPECT_TRUE(a);
        EXPECT_EQ(a, 42);
        EXPECT_FALSE(b);
    }
    {
        auto [a, b] = co_await whenAny(sleep(10ms), returnInput(42));
        EXPECT_FALSE(a);
        EXPECT_EQ(b, 42);
    }
    {
        auto [a, b] = co_await whenAny(sleep(10ms), sleep(20ms));
        EXPECT_TRUE(a);
        EXPECT_FALSE(b);
    }

    // Check stopping
    auto handle = spawn([&]() -> Task<void> {
        auto _ = co_await whenAny(sleep(10ms), sleep(20ms));
        ::abort(); // should not be reached
    });
    handle.stop();
    EXPECT_TRUE(!co_await std::move(handle));
    co_return;
}

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    runtime::EventLoop loop;
    loop.install();
    return RUN_ALL_TESTS();
}