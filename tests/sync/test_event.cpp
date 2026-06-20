#include <ilias/sync/event.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>

using namespace ilias;
using namespace std::literals;

ILIAS_TEST(Sync, Event) {
    Event event;
    
    EXPECT_FALSE(event.isSet());
    event.set();
    event.set(); // Test set again
    EXPECT_TRUE(event.isSet());

    co_await event;

    // Try wait on it
    event.clear();
    auto fn = [&]() -> Task<void> {
        co_await event;  
    };
    auto handle = spawn(fn());
    co_await sleep(10ms);
    event.set(); // Wake up the task
    
    EXPECT_TRUE(co_await std::move(handle));

    // Auto Clear Event
    Event event2 {Event::AutoClear};
    EXPECT_FALSE(event2.isSet());

    event2.set();
    EXPECT_TRUE(event2.isSet());

    co_await event2;
    EXPECT_FALSE(event2.isSet());

    // Try wait on it
    auto fn2 = [&]() -> Task<void> {
        co_await event2;
        EXPECT_FALSE(event2.isSet());
    };
    handle = spawn(fn2());
    co_await sleep(10ms);
    event2.set(); // Wake up the task

    EXPECT_TRUE(co_await std::move(handle));
    EXPECT_FALSE(event2.isSet());
}
