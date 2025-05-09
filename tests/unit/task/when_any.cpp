#include <ilias/task/when_any.hpp>
#include <ilias/task/decorator.hpp>
#include <ilias/task/mini_executor.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace std::chrono_literals;

template <typename T>
auto returnInput(T input) -> Task<T> {
    co_return input;
}

TEST(WhenAny, Basic) {
    {
        auto [a, b, c] = whenAny(sleep(20ms), sleep(10ms), sleep(15ms)).wait();
        EXPECT_FALSE(a);
        EXPECT_TRUE(b);
        EXPECT_FALSE(c);
    }
    {
        auto [a, b, c] = whenAny(sleep(10ms), sleep(20ms), sleep(15ms)).wait();
        EXPECT_TRUE(a);
        EXPECT_FALSE(b);
        EXPECT_FALSE(c);
    }

}

TEST(WhenAny, Basic1) {
    {
        auto [a, b, c] = whenAny(sleep(10ms), returnInput(2), sleep(10ms)).wait();
        EXPECT_FALSE(a);
        EXPECT_TRUE(b);
        EXPECT_FALSE(c);
    }
    {
        auto [a, b, c] = whenAny(returnInput(1), sleep(10ms), sleep(10ms)).wait();
        EXPECT_TRUE(a);
        EXPECT_FALSE(b);
        EXPECT_FALSE(c);
    }
    {
        auto [a, b, c] = whenAny(sleep(10ms), sleep(10ms), returnInput(3)).wait();
        EXPECT_FALSE(a);
        EXPECT_FALSE(b);
        EXPECT_TRUE(c);
    }

    {
        auto [a, b, c] = whenAny(std::suspend_never{}, returnInput(2), sleep(10ms)).wait();
        EXPECT_TRUE(a);
        EXPECT_FALSE(b);
        EXPECT_FALSE(c);
    }

    {
        auto [a, b, c] = whenAny(sleep(10ms), backtrace(), returnInput(3)).wait();
        EXPECT_FALSE(a);
        EXPECT_TRUE(b);
        EXPECT_FALSE(c);
    }
}

TEST(WhenAny, Range) {
    {
        std::vector<IoTask<void> > tasks;
        tasks.emplace_back(sleep(20ms));
        tasks.emplace_back(sleep(10ms));
        tasks.emplace_back(sleep(15ms));
        auto res = whenAny(std::move(tasks)).wait();
        EXPECT_TRUE(res);
    }
}

TEST(Decorator, SetTimeout) {
    auto res = (returnInput(10) | setTimeout(10s)).wait();
    ASSERT_TRUE(res);
}

TEST(Decorator, IgnoreCancellation) {
    auto job = []() -> Task<void> {
        auto val = co_await (sleep(50ms) | ignoreCancellation);
        ILIAS_ASSERT(val); // Must true, not cancelled
    };
    (job() | setTimeout(10ms)).wait();
}

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    MiniExecutor exec;

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}