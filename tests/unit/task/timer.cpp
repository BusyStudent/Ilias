#include <ilias/task/mini_executor.hpp>
#include <gtest/gtest.h>
#include <chrono>

using namespace std::chrono_literals;
using namespace ILIAS_NAMESPACE;

TEST(Timer, Sleep) {
    MiniExecutor executor;

    auto now = std::chrono::steady_clock::now();
    sleep(1000ms).wait();
    auto diff = std::chrono::steady_clock::now() - now;

    now = std::chrono::steady_clock::now();
    sleep(10ms).wait();
    diff += std::chrono::steady_clock::now() - now;
}

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}