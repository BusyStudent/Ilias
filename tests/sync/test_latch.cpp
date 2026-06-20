#include <ilias/sync/latch.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>

using namespace ilias;
using namespace std::literals;

// Latch
ILIAS_TEST(Sync, Latch) {
    Latch latch {3};
    auto fn = [&]() -> Task<void> {
        co_await latch.arriveAndWait();
    };
    auto _ = co_await whenAll(fn(), fn(), fn());
    EXPECT_TRUE(latch.tryWait()); // count is 0
}
