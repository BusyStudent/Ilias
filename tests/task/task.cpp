#include <ilias/task/generator.hpp>
#include <ilias/task/when_all.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/task/utils.hpp>
#include <ilias/task/task.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>

using namespace ilias;
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
        ILIAS_TRAP(); // Should never reach this line
    }
    catch (int val) {
        EXPECT_EQ(val, 42);
    }
    
    co_await sleep(10ms);
    co_await sleep(20ms);
    co_return;
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
    
    // Check lifetime
    // When the spawn task is completed, the argument & captured value should be destroyed
    auto value = std::make_shared<int>(42);
    auto weak = std::weak_ptr {value};
    auto handle = spawn([value = std::move(value)]() -> Task<int> {
        co_return *value;
    });
    auto stopHandle = StopHandle {handle};
    auto val = std::move(handle).wait();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(val, 42);
}

TEST(Task, SpawnAwait) {
    auto fn = []() -> Task<void> {
        co_await spawn(testTask());
    };
    fn() | blockingWait(); // Try tags invoke here
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

auto range(int start, int end) -> Generator<int> {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

ILIAS_TEST(Task, Generator) {
    ilias_for_await(int i, range(0, 10)) {
        EXPECT_TRUE(i >= 0 && i < 10);
    }

    {
        auto gen = range(0, 10);
        ilias_for_await(int i, gen) {
            EXPECT_TRUE(i >= 0 && i < 10);
        }
    }

    {
        auto gen = Generator<int> {};
        gen = range(0, 10);
        ilias_for_await(int i, gen) {
            EXPECT_TRUE(i >= 0 && i < 10);
        }
    }
}

ILIAS_TEST(Task, WhenAll) {
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
    {
        // Check when all on multi blocking task
        auto blockingSleep = []() {
            std::this_thread::sleep_for(100ms);
        };
        auto [a, b, c, d] = co_await whenAll(blocking(blockingSleep), blocking(blockingSleep), blocking(blockingSleep), blocking(blockingSleep));
    }

    // Check stopping
    auto handle = spawn([]() -> Task<void> {
        auto _ = co_await whenAll(sleep(10ms), sleep(20ms));
        ILIAS_TRAP(); // should not be reached
    });
    handle.stop();
    EXPECT_TRUE(!co_await std::move(handle));
}

ILIAS_TEST(Task, WhenAny) {
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
        ILIAS_TRAP(); // should not be reached
    });
    handle.stop();
    EXPECT_TRUE(!co_await std::move(handle));
    co_return;
}

ILIAS_TEST(Task, Executor) {
    auto &&executor = co_await this_coro::executor();
    executor.schedule([]() {
        std::cout << "Hello from executor!" << std::endl;
    });
    executor.schedule([i = 114514]() {
        std::cout << "Hello from executor with value " << i << std::endl;
    });
    executor.schedule([a = std::string("Hello World")]() {
        std::cout << "Hello from executor with value " << a << std::endl;
    });
    co_await this_coro::yield(); // Return to the executor
    co_return;
}

ILIAS_TEST(Task, Stacktrace) {
    auto fn = []() -> Task<void> {
        auto trace = co_await this_coro::stacktrace() ;
        std::cout << trace.toString() << std::endl;
    };
    std::cout << "Stacktrace for basic fn" << std::endl;
    co_await fn();

    // Check spawn
    std::cout << "Stacktrace for spawn fn" << std::endl;
    auto handle = spawn(fn());
    co_await std::move(handle);

    // Check with whenAny & whenAll
    std::cout << "Stacktrace for whenAny" << std::endl;
    std::ignore = co_await whenAll(fn());

    std::cout << "Stacktrace for whenAny" << std::endl;
    std::ignore = co_await whenAny(fn());
}

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    EventLoop loop;
    loop.install();
    return RUN_ALL_TESTS();
}