#include <ilias/task/mini_executor.hpp>
#include <ilias/task/task.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

template <typename T>
auto returnInput(T in) -> Task<T> {
    co_return in;
}

TEST(Task, Wait) {
    auto value = []() -> Task<int> {
        co_return co_await returnInput(42);
    }().wait().value();

    EXPECT_EQ(value, 42);
}

TEST(Task, Exception) {
    auto value = []() -> Task<int> {
        throw 1;
        co_return {};
    };
    EXPECT_THROW(value().wait(), int);
}

TEST(Task, Exception2) {
    auto value = []() -> Task<int> {
        Result<void> result {
            Unexpected(Error::Unknown)
        };
        result.value(); //< this will throw
        co_return {};
    };
    EXPECT_NO_THROW(ilias_wait value());
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    MiniExecutor executor;
    return RUN_ALL_TESTS();
}