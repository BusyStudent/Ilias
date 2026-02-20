#include <ilias/runtime/executor.hpp>
#include <ilias/fiber.hpp>
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
    ASSERT_EQ(std::move(fiber).wait(), 42);

    auto fiber2 = Fiber([s = "HelloWorld"]() {
        return std::string {s};
    });
    ASSERT_EQ(std::move(fiber2).wait(), "HelloWorld");

    auto fiber3 = Fiber([]() {
        throw std::exception {};
    });
    ASSERT_THROW(std::move(fiber3).wait(), std::exception);
}

TEST(Fiber, Await) {
    auto fiber = Fiber([]() {
        this_fiber::await(sleep(10ms));
    });
    std::move(fiber).wait();
}

ILIAS_TEST(Fiber, Spawn) {
    auto fiber = Fiber([]() {
        return 42;
    });
    auto handle = spawn(toTask(std::move(fiber)));
    EXPECT_EQ(co_await std::move(handle), 42);

    // With stop
    auto fiber2 = Fiber([]() {
        this_fiber::await(sleep(1000ms));
        // Never reached
        ILIAS_TRAP();
    });
    auto handle2 = spawn(toTask(std::move(fiber2)));
    handle2.stop();
    EXPECT_FALSE(co_await std::move(handle2));
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
    loop.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}