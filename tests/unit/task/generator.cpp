#include <ilias/task/mini_executor.hpp>
#include <ilias/task/generator.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(Generator, Basic) {
    auto gen = []() -> Generator<int> {
        for (int i = 0; i < 10; i++) {
            if (i % 2) co_await sleep(1ms);
            co_yield i;
        }
        co_await backtrace();
    };
    auto fn = [&]() -> Task<> {
        ilias_foreach(const int &i, gen()) {
            std::cout << i << std::endl;
        }
    };
    fn().wait();

    auto res = gen().collect().wait();
    auto cmp = std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_EQ(res, cmp);
}

TEST(Generator, Empty) {
    auto gen = []() -> Generator<int> {
        co_return;
    };
    EXPECT_TRUE(gen().collect().wait().empty());
}

TEST(Generator, Exception) {
    auto gen = []() -> Generator<int> {
        throw 1;
        co_return;
    };

    EXPECT_THROW(gen().collect().wait(), int);
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    MiniExecutor executor;
    return RUN_ALL_TESTS();
}