#include <ilias/task/mini_executor.hpp>
#include <ilias/task/spawn.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(TaskSpawn, SpawAndWait) {
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
    int value = 0;
    spawn([&]() -> Task<> {
        value = 1;
        co_return {};
    });
    spawn([&]() -> Task<> {
        co_await sleep(10ms);
        co_return {};
    }).wait();

    ASSERT_EQ(value, 1);
}

TEST(TaskSpawn, Await) {
    auto handle = spawn([]() -> Task<int> {
        co_return 42;
    });
    auto value = [&]() -> Task<int> {
        co_return co_await std::move(handle);
    }().wait().value();

    ASSERT_EQ(value, 42);
}

TEST(TaskSpawn, Macro) {
    auto fn = []() -> Task<int> {
        co_return 42;
    };
    auto answer = ilias_go fn();
    ASSERT_EQ(answer.wait().value(), 42);
}

auto main(int argc, char** argv) -> int {
    MiniExecutor exec;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}