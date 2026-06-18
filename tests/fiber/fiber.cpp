#include <ilias/runtime/executor.hpp>
#include <ilias/fiber.hpp>
#include <ilias/task.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>
#include <iostream>

using namespace std::literals;
using namespace ilias;

#if defined(ILIAS_USE_FIBER)

TEST(Fiber, Simple) {
    auto fiber = Fiber([]() {
        std::cout << "Hello, World! from fiber" << std::endl;
        return 42;
    });
    ASSERT_EQ(fiber.wait(), 42);

    auto fiber2 = Fiber([s = "HelloWorld", other = std::make_unique<int>(42)]() {
        return std::string {s} + " " + std::to_string(*other);
    });
    ASSERT_EQ(fiber2.wait(), "HelloWorld 42");

    auto fiber3 = Fiber([]() {
        throw std::exception {};
    });
    ASSERT_THROW(fiber3.wait(), std::exception);
}

TEST(Fiber, Await) {
    auto fiber = Fiber([]() {
        this_fiber::await(sleep(10ms));

        // Test WhenAny
        auto [a, b] = whenAny(
            sleep(10ms),
            sleep(1s)
        ) | this_fiber::await;
        EXPECT_TRUE(a);
        EXPECT_FALSE(b);
    });
    fiber.wait();
}

ILIAS_TEST(Fiber, Spawn) {
    auto fiber = Fiber([]() {
        return 42;
    });
    auto handle = spawn(toTask(std::move(fiber)));
    EXPECT_EQ(co_await std::move(handle), 42);

    // With stop
    auto reached = false;
    auto fiber2 = Fiber([&]() {
        auto token = this_fiber::stopToken();
        auto callback = runtime::StopCallback(token, [&]() {
            reached = true;
        });
        this_fiber::await(sleep(1000ms));
        // Never reached
        ILIAS_TRAP();
    });
    auto handle2 = spawn(toTask(std::move(fiber2)));
    handle2.stop();
    EXPECT_FALSE(co_await std::move(handle2));
    EXPECT_TRUE(reached);
}

ILIAS_TEST(FiberAwait, Await) {
    auto fiber = Fiber([](int value) {
        return value;
    }, 42);
    EXPECT_EQ(co_await std::move(fiber), 42);

    auto fiber2 = Fiber([]() {
        std::string s = "HelloWorld";
        this_fiber::yield();
        return s;
    });
    EXPECT_EQ(co_await std::move(fiber2), "HelloWorld");
}

#endif // ILIAS_USE_FIBER

auto main(int argc, char **argv) -> int {
    EventLoop loop;
    FiberInitializer init;
    loop.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}