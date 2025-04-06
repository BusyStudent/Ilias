#include <ilias/task/when_all.hpp>
#include <ilias/task/mini_executor.hpp>
#include <gtest/gtest.h>

using namespace std::chrono_literals;
using namespace ILIAS_NAMESPACE;

template <typename T>
auto returnInput(T input) -> Task<T> {
    co_return input;
}

TEST(WhenAll, Basic) {
    {
        auto [a, b, c] = whenAll(returnInput(1), returnInput(2), returnInput(3)).wait();
        ASSERT_EQ(a, 1);
        ASSERT_EQ(b, 2);
        ASSERT_EQ(c, 3);
    }
    {
        auto [a, b, c] = whenAll(sleep(1ms), returnInput(2), returnInput(3)).wait();
        ASSERT_TRUE(a);
        ASSERT_EQ(b, 2);
        ASSERT_EQ(c, 3);
    }
    {
        auto [a, b, c] = whenAll(sleep(20ms), sleep(10ms), returnInput(30ms)).wait();
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        ASSERT_EQ(c, 30ms);
    }

    {
        auto [a, b, c] = whenAll(std::suspend_never{}, returnInput(2), returnInput(3)).wait();
        ASSERT_EQ(b, 2);
        ASSERT_EQ(c, 3);
    }
    {
        auto [a, b, c] = whenAll(sleep(1ms), backtrace(), returnInput(30ms)).wait();
        ASSERT_TRUE(a);
        ASSERT_EQ(c, 30ms);
    }
}

TEST(WhenAll, Range) {
    {
        std::vector<Task<int> > tasks;
        tasks.emplace_back(returnInput(1));
        tasks.emplace_back(returnInput(2));
        tasks.emplace_back(returnInput(3));
        auto vec = whenAll(std::move(tasks)).wait();
        ASSERT_EQ(vec.size(), 3);
        ASSERT_EQ(vec[0], 1);
        ASSERT_EQ(vec[1], 2);
        ASSERT_EQ(vec[2], 3);
    }
    {
        std::vector<IoTask<void> > tasks;
        tasks.emplace_back(sleep(1ms));
        tasks.emplace_back(sleep(2ms));
        tasks.emplace_back(sleep(3ms));
        auto vec = whenAll(std::move(tasks)).wait();
        ASSERT_EQ(vec.size(), 3);
        ASSERT_TRUE(vec[0]);
        ASSERT_TRUE(vec[1]);
        ASSERT_TRUE(vec[2]);
    }
}

auto main(int argc, char **argv) -> int {
    MiniExecutor exec;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}