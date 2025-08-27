#include <ilias/task/group.hpp>
#include <ilias/task/utils.hpp>
#include <ilias/task/scope.hpp>
#include <ranges>
#include "testing.hpp"

using namespace std::literals;
namespace views = std::views;

auto neverReturn() -> Task<void> {
    while (true) {
        co_await sleep(100ms);
    }
}

auto returnBeforeSleep(int x) -> Task<int> {
    co_await sleep(std::chrono::milliseconds(x));
    co_return x;
}

CORO_TEST(Task, TaskGroup) {
    {
        auto group = TaskGroup<void>();
        group.spawn(neverReturn());
        co_await group.shutdown();
    }

    {
        auto group = TaskGroup<void>();
        group.spawn(neverReturn());
        group.stop();
        EXPECT_FALSE((co_await group.next()).has_value());
    }

    {
        auto group = TaskGroup<int>();
        for (auto i : views::iota(0, 10)) {
            group.spawn(returnBeforeSleep(i));
        }
        auto result = co_await group.waitAll();
        EXPECT_EQ(result.size(), 10);
    }

    {
        auto group = TaskGroup<void>();
        group.spawn(sleep(10ms));
    }

    { // Test Stop
        auto fn = []() -> Task<void> {
            auto group = TaskGroup<void>();
            for (auto i : views::iota(1, 100)) {
                group.spawn(sleep(1s * i));
            }
            auto group2 = std::move(group); // Test Group move
            auto result = co_await group2.waitAll();
            ::abort(); // Should not reach here
        };
        auto handle = spawn(fn());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
    co_return;
}

CORO_TEST(Task, Unstoppable) {
    auto fn = []() -> Task<void> {
        co_await unstoppable(sleep(10ms));
    };
    auto handle = spawn(fn());
    handle.stop();
    auto result = co_await std::move(handle);
    EXPECT_TRUE(result.has_value());
}

CORO_TEST(Task, Finally) {
    { // Normal condition
        bool called = false;
        auto onfinally = [&]() -> Task<void> {
            called = true;
            co_return;
        };
        co_await (sleep(10ms) | finally(onfinally));
        EXPECT_TRUE(called);
    }
    { // Stop condition
        bool called = false;
        auto fn = [&]() -> Task<void> {
            auto onfinally = [&]() -> Task<void> {
                called = true;
                co_return;
            };
            co_await finally(sleep(10ms), onfinally);
            ::abort(); // Should not reach here
        };
        auto handle = spawn(fn());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
        EXPECT_TRUE(called);
    }
}

CORO_TEST(Task, Scope) {
    co_await TaskScope::enter([](TaskScope &scope) -> Task<void> {
        for (auto i : views::iota(1, 100)) {
            scope.spawn(sleep(1ms * i));
        }
        scope.spawnBlocking([]() { return 42; }); // The return value should be ignored
        co_return;
    });
}

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    EventLoop loop;
    loop.install();
    return RUN_ALL_TESTS();
}