#include <ilias/task/task.hpp>
#include <gtest/gtest.h>

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
    co_await sleep(1s);
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

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    runtime::EventLoop loop;
    loop.install();
    return RUN_ALL_TESTS();
}