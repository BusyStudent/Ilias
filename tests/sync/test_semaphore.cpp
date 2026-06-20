#include <ilias/sync/semaphore.hpp>
#include <ilias/testing.hpp>

using namespace ilias;
using namespace std::literals;

ILIAS_TEST(Sync, Semaphore) {
    Semaphore sem(10);
    auto premit = co_await sem.acquire();
    EXPECT_EQ(sem.available(), 9);
    auto premit2 = co_await sem.acquire();
    EXPECT_EQ(sem.available(), 8);

    auto fn = [&]() -> Task<void> {
        auto group = TaskGroup<void>();
        for (int i = 0; i < 100; ++i) {
            group.spawn([&]() -> Task<void> {
                auto _ = co_await sem.acquire();
                co_await sleep(10ms);
            });
        }
        auto result = co_await group.waitAll();
        EXPECT_EQ(result.size(), 100);
    };

    // Try cross thread & local thread to acquire this semaphore
    auto exec = useExecutor<EventLoop>();
    auto thread = Thread(exec, fn);
    auto _ = co_await whenAll(fn(), thread.join());
    EXPECT_EQ(sem.available(), 8);
}
