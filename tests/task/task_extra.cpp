#include <ilias/task/group.hpp>
#include <ilias/task/utils.hpp>
#include <ilias/task/scope.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>
#include <ranges>

using namespace std::literals;
using namespace ILIAS_NAMESPACE;
namespace views = std::views;

auto neverReturn() -> Task<void> {
    while (true) {
        co_await sleep(100ms);
    }
}

auto returnAfterSleep(int x) -> Task<int> {
    co_await sleep(std::chrono::milliseconds(x));
    co_return x;
}

ILIAS_TEST(Task, TaskGroup) {
    {
        auto group = TaskGroup<void>();
        group.spawn(neverReturn());
        co_await this_coro::yield(); // Make sure the neverReturn task is running
        co_await group.shutdown();
    }

    {
        auto group = TaskGroup<void>();
        group.spawn(neverReturn());
        co_await this_coro::yield(); // As shown above
        group.stop();
        EXPECT_FALSE((co_await group.next()).has_value());
    }

    {
        auto group = TaskGroup<int>();
        for (auto i : views::iota(0, 10)) {
            group.spawn(returnAfterSleep(i));
        }
        auto result = co_await group.waitAll();
        EXPECT_EQ(result.size(), 10);
    }

    {
        auto group = TaskGroup<void>();
        group.spawn(sleep(10ms));
    }

    {
        auto group = TaskGroup<void>();
        group.spawn(sleep(10h));
        co_await this_coro::yield();
        group.stop();
    }

    {
        // Test already stopped group
        auto group = TaskGroup<void>();
        group.stop();
        for (auto i : views::iota(1, 100)) {
            group.spawn(sleep(1s * i));
            if (i % 2) { // Randomly back to executor
                co_await this_coro::yield();
            }
        }
        auto _ = co_await group.waitAll();
    }

    { // Test Stop
        auto fn = []() -> Task<void> {
            auto group = TaskGroup<void>();
            for (auto i : views::iota(1, 100)) {
                group.spawn(sleep(1s * i));
                co_await this_coro::yield();
            }
            auto group2 = std::move(group); // Test Group move
            auto result = co_await group2.waitAll();
            ILIAS_TRAP(); // Should not reach here
        };
        auto handle = spawn(fn());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
    co_return;
}

ILIAS_TEST(Task, Unstoppable) {
    auto fn = []() -> Task<void> {
        co_await unstoppable(sleep(10ms));
    };
    auto handle = spawn(fn());
    handle.stop();
    auto result = co_await std::move(handle);
    EXPECT_TRUE(result.has_value());
}

ILIAS_TEST(Task, Finally) {
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
            ILIAS_TRAP(); // Should not reach here
        };
        auto handle = spawn(fn());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
        EXPECT_TRUE(called);
    }
}

ILIAS_TEST(Task, Mapping) {
    auto result = co_await (
        returnAfterSleep(10) 
        | fmap([](int x) { return x * 2; })
        | fmap([](int x) { return x + 10; })
    );
    EXPECT_EQ(result, 30);
}

ILIAS_TEST(Task, FireAndForget) {
    auto fn = []() -> FireAndForget {
        co_await sleep(10ms);
    };
    fn();
    co_await this_coro::yield();
}

ILIAS_TEST(Task, Scope) {
    // Normal
    co_await TaskScope::enter([](TaskScope &scope) -> Task<void> {
        for (auto i : views::iota(1, 100)) {
            scope.spawn(sleep(1ms * i));
            co_await this_coro::yield();
        }
        scope.spawnBlocking([]() { return 42; }); // The return value should be ignored
        co_return;
    });

    // Test stop from inside
    co_await TaskScope::enter([](TaskScope &scope) -> Task<void> {
        scope.stop();
        for (auto i : views::iota(1, 100)) {
            scope.spawn(sleep(1h * i));
        }
        co_return;
    });

    {
        // Test stop from the outside
        auto handle = spawn(TaskScope::enter([](TaskScope &scope) -> Task<void> {
            for (auto i : views::iota(1, 100)) {
                scope.spawn(sleep(1h * i));
            }
            co_await sleep(1h);
        }));
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle)); // Should be stopped
    }

    {
        // Test stop by manually waitAll
        auto handle = spawn([]() -> Task<void> {
            auto scope = TaskScope();
            for (auto i : views::iota(1, 100)) {
                scope.spawn(sleep(1h * i));
            }
            co_await scope.waitAll();
        }());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle)); // Should be stopped
    }
}

ILIAS_TEST(Task, AsyncLifetime) {
    struct Value : public AsyncLifetime<Value> {
        ~Value() {
            EXPECT_TRUE(scope().empty());
        }
    };
    auto ptr = makeAsyncLifetime<Value>();
    co_await this_coro::yield();
    co_return;
}

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    EventLoop loop;
    loop.install();
    return RUN_ALL_TESTS();
}