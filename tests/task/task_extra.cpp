#include <ilias/task/thread.hpp>
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

    { // Test already completed handle
        auto group = TaskGroup<void>();
        auto handle = spawn([]() -> Task<void> {
            co_return;
        });
        co_await sleep(10ms);
        group.insert(std::move(handle)); // This handle should alreayd be completed

        co_await group.shutdown();
    }
    co_return;
}

ILIAS_TEST(Task, WhenAllSequence) {
    auto vec = std::vector<Task<int> > {};
    for (auto i : views::iota(0, 10)) {
        vec.emplace_back(returnAfterSleep(i));
    }
    auto result = co_await whenAll(std::move(vec));
    EXPECT_EQ(result.size(), 10);
}

ILIAS_TEST(Task, WhenAnySequence) {
    auto vec = std::vector<Task<int> > {};
    for (auto i : views::iota(0, 10)) {
        vec.emplace_back(returnAfterSleep(i));
    }
    auto result = co_await whenAny(std::move(vec));
    EXPECT_TRUE(result < 10 && result >= 0);
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

ILIAS_TEST(Task, Thread) {
    auto sleep1h = []() -> Task<void> {
        co_await sleep(1h);  
    };

    // Test normal
    auto exec = useExecutor<EventLoop>();
    auto thread = Thread(exec, [](int in) -> Task<int> {
        co_return in;
    }, 42);
    auto res = co_await thread.join();
    EXPECT_EQ(res, 42);

    // Test exception
    auto thread2 = Thread(exec, []() -> Task<void> {
        throw std::runtime_error("test");
        co_return;
    });
    EXPECT_THROW(co_await thread2.join(), std::runtime_error);

    // Test stop
    thread2 = Thread(sleep1h);
    thread2.stop();
    EXPECT_FALSE(co_await thread2.join()); // nullopt

    // Test stop from upper
    auto fn = [&]() -> Task<void> {
        co_await Thread(sleep1h);
    };
    auto handle = spawn(fn());
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));

    // Test finish before wait
    auto thread3 = Thread(exec, []() -> Task<void> {
        co_return; 
    });
    thread3.setName("thread3"); // Test set the name of it
    co_await sleep(10ms);
    EXPECT_TRUE(co_await thread3.join());
}

ILIAS_TEST(Task, StopToken) {
    {
        // Test cancel
        auto stopSource = std::stop_source {};
        auto [stop, other] = co_await whenAny(stopSource.get_token(), std::suspend_never{});
        EXPECT_FALSE(stop);
        EXPECT_TRUE(other);
    }

    {
        // Test normal condition
        auto stopSource = std::stop_source {};
        auto handle = spawn([token = stopSource.get_token()]() -> Task<void> {
            co_return co_await token;
        });
        co_await sleep(10ms);
        stopSource.request_stop();
        EXPECT_TRUE(co_await std::move(handle)); // This token is stopped
    }
}

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    EventLoop loop;
    loop.install();
    return RUN_ALL_TESTS();
}