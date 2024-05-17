// #define ILIAS_COROUTINE_TRACE
#define ILIAS_COROUTINE_LIFETIME_CHECK
#include "../include/ilias_task.hpp"
#include "../include/ilias_await.hpp"
#include "../include/ilias_channel.hpp"
#include "../include/ilias_loop.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <string>

using namespace ILIAS_NAMESPACE;
using namespace std::chrono_literals;

TEST(TaskTest, GetValue) {
    auto num = ilias_wait []() -> Task<int> {
        co_return co_await []() -> Task<int> {
            co_return 1;
        }();
    }();
    ASSERT_EQ(num.value(), 1);
}
TEST(TaskTest, Impl1) {
    auto task = []() -> Task<> {
        co_return Result<>();
    };
    auto v = task();
    ASSERT_EQ(v.promise().isStarted(), false);
    ilias_wait v;
    ASSERT_EQ(v.promise().isStarted(), true);
    ASSERT_EQ(v.handle().done(), true);
}

TEST(WhenAllTest, Test1) {
    auto task = []() -> Task<> {
        co_await WhenAll(Sleep(1s), Sleep(10ms));
        co_return Result<>();
    };
    auto now = std::chrono::steady_clock::now();
    ilias_wait task();
    auto diff =  std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now);
    ASSERT_LT(diff, 1100ms);
}

// ---- Test for Channels
// send n, return actually sended
auto sendForN(Sender<int> s, int n) -> Task<int> {
    int i = 0;
    for (i = 0; i < n; ++i) {
        if (auto v = co_await s.send(i); !v) {
            ::printf("sendForN: send failed =>%s\n", v.error().message().c_str());
            break;
        }
    }
    s.close();
    co_return i;
}

auto printUtilNone(Receiver<int> r) -> Task<int> {
    int i = 0;
    while (auto num = co_await r.recv()) {
        // ::printf("%d\n", num.value());
        i += 1;
    }
    r.close();
    co_return i;
}

auto printUtilN(Receiver<int> r, int n) -> Task<int> {
    int i = 0;
    while (auto num = co_await r.recv()) {
        // ::printf("%d\n", num.value());
        n -= 1;
        i += 1;
        if (n == 0) {
            break;
        }
    }
    r.close();
    co_return i;
}

TEST(ChannelTest, PrintUntilClosed) {
    auto task = [](size_t capicity = 32, size_t n = 30) -> Task<std::pair<int, int> > {
        auto [sx, rx] = Channel<int>::make(32);
        auto [a, b] = co_await WhenAll(
            sendForN(std::move(sx), n),
            printUtilNone(std::move(rx))
        );
        co_return std::pair{a.value(), b.value()};
    };

    {
    auto [a, b] = (ilias_wait task()).value();
    ASSERT_EQ(a, 30);
    ASSERT_EQ(b, 30);
    }

    {
    auto [a, b] = (ilias_wait task(1)).value();
    ASSERT_EQ(a, 30);
    ASSERT_EQ(b, 30);
    }

    {
    auto [a, b] = (ilias_wait task(4)).value();
    ASSERT_EQ(a, 30);
    ASSERT_EQ(b, 30);
    }

    {
    auto [a, b] = (ilias_wait task(4, 114514)).value();
    ASSERT_EQ(a, 114514);
    ASSERT_EQ(b, 114514);
    }
}

int main(int argc, char **argv) {
    MiniEventLoop loop;

    ilias_spawn [v = 11]() -> Task<int> {
        std::cout << "task spawn " << v << std::endl;
        co_return v;
    };

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}