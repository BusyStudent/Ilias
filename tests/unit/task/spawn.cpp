#include <ilias/task/mini_executor.hpp>
#include <ilias/task/spawn.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(TaskSpawn, SpawAndWait) {
    MiniExecutor exec;
    auto callable = []() -> Task<int> {
        co_return 42;
    };
    auto value1 = spawn(callable).wait();
    ASSERT_EQ(value1.value(), 42);

    auto callableWithCapture = [v = 0]() -> Task<int> {
        co_return v;
    };
    auto value2 = spawn(callableWithCapture).wait();
    ASSERT_EQ(value2.value(), 0);
}

TEST(TaskSpawn, Detach) {
    MiniExecutor exec;
    int value = 0;
    spawn([&]() -> Task<> {
        value = 1;
        co_return {};
    });
    spawn([&]() -> Task<> {
        co_await exec.sleep(10);
        co_return {};
    }).wait();

    ASSERT_EQ(value, 1);
}

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}