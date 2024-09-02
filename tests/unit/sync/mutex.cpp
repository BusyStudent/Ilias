#include <ilias/task/mini_executor.hpp>
#include <ilias/task/when_all.hpp>
#include <ilias/task/task.hpp>
#include <ilias/sync/mutex.hpp>

#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(Mutex, Basic) {
    Mutex mtx;
    bool value = true;
    auto fn1 = [&]() -> Task<void> {
        co_await yield();
        auto val = co_await mtx.lock();
        if (!val) {
            co_return Unexpected(val.error());
        }
        value = false;
        mtx.unlock();
        co_return {};
    };
    auto fn2 = [&]() -> Task<void> {
        auto val = co_await mtx.uniqueLock();
        if (!val) {
            co_return Unexpected(val.error());
        }
        value = true;
        co_await sleep(100ms);
        co_return {};
    };
    auto [val1, val2] = whenAll(fn1(), fn2()).wait();
    ASSERT_TRUE(val1);
    ASSERT_TRUE(val2);
}

TEST(Mutex, Death) {
    EXPECT_DEATH({
        Mutex mtx;
        mtx.unlock();
    }, "");
}

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_LOG_ADD_WHITELIST("Mutex"); //< Only log mutex related messages
    MiniExecutor exec;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}