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

TEST(Task, Create) {
    auto fn = []() -> Task<void> {
        co_return {};  
    };
    auto task = fn(); //< Just create    
}

TEST(TaskDeathTest, Crash) {

#if defined(NDEBUG)
    GTEST_SKIP(); //< This task below will trigger assert, so it won't crash on release
#endif // defined(NDEBUG)

    auto fn = []() -> Task<void> {
        co_await std::suspend_always();
        co_return {};
    };
    EXPECT_DEATH_IF_SUPPORTED({
        auto task = fn();
        auto view = task._view();
        view.resume(); //< Resume the task, but the executor is not set, it will crash
    }, "");

    EXPECT_DEATH_IF_SUPPORTED({
        auto task = fn();
        auto view = task._view();
        view.schedule();
    }, "");

    EXPECT_DEATH_IF_SUPPORTED({
        auto task = fn();
        auto view = task._view();
        view.setExecutor(Executor::currentThread());
        view.resume();  //< When task destroys, this will cause a crash, destroy a started and not finished task
    }, "");
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    MiniExecutor executor;
    return RUN_ALL_TESTS();
}