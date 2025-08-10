#include <ilias/runtime/executor.hpp>
#include <ilias/fiber/fiber.hpp>
#include "testing.hpp"
#include <iostream>

using namespace std::literals;

TEST(Fiber, Internal) {
    auto fiber = fiber::FiberContext::create([]() {
        std::string s = "HelloWorld";
        // suspend here
        fiber::FiberContext::suspend();
        return s;
    });
    fiber->resume();
    ASSERT_FALSE(fiber->done()); // fiber is not done
    fiber->destroy(); // Test destry on the suspend point
}

TEST(Fiber, Simple) {
    auto fiber = Fiber([]() {
        std::cout << "Hello, World! from fiber" << std::endl;
        return 42;
    });
    ASSERT_EQ(std::move(fiber).wait(), 42);

    auto fiber2 = Fiber([s = "HelloWorld"]() {
        return std::string(s);
    });
    ASSERT_EQ(std::move(fiber2).wait(), "HelloWorld");

    auto fiber3 = Fiber([]() {
        throw std::exception("String :)");
    });
    ASSERT_THROW(std::move(fiber3).wait(), std::exception);
}

TEST(Fiber, Await) {
    auto fiber = Fiber([]() {
        this_fiber::await(sleep(10ms));
    });
    std::move(fiber).wait();
}

CORO_TEST(FiberAwait, Await) {
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

auto main(int argc, char **argv) -> int {
    runtime::EventLoop loop;
    loop.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}