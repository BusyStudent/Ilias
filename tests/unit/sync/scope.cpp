#include <ilias/task/mini_executor.hpp>
#include <ilias/sync/scope.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(Scope, OutScope) {
    bool value = false;
    {
        TaskScope scope;
        auto handle = scope.spawn([]() -> Task<void> {
            std::cout << "From spawned task1" << std::endl;
            co_return {};
        });
        auto handle2 = scope.spawn([&]() -> Task<void> {
            value = true;
            co_return {};
        });
    }
    ASSERT_TRUE(value);
}

TEST(Scope, Wait) {
    bool value = false;
    TaskScope::WaitHandle<> handle;
    {
        TaskScope scope;
        scope.spawn([&]() -> Task<void> {
            value = true;
            co_return {};
        }).wait();
        ASSERT_TRUE(value);

        handle = scope.spawn([]() -> Task<void> {
            std::cout << "From spawned task2" << std::endl;
            co_return {};
        });
    }
    ASSERT_TRUE(handle);
    ASSERT_TRUE(handle.done());
}

auto main(int argc, char **argv) -> int {
    MiniExecutor executor;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
