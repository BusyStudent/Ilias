#include <ilias/sync/oneshot.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>

using namespace ilias;
using namespace std::literals;

ILIAS_TEST(Sync, Oneshot) {
    {
        auto [sender, receiver] = oneshot::channel<int>();
        EXPECT_FALSE(receiver.tryRecv());
        EXPECT_TRUE(sender.send(42));
        EXPECT_EQ(co_await std::move(receiver), 42);
    }
    { // close
        auto [sender, receiver] = oneshot::channel<int>();
        sender.close();
        EXPECT_FALSE(co_await std::move(receiver));
    }
    { // blocking
        auto [sender, receiver] = oneshot::channel<int>();
        auto recv = [&]() -> Task<std::optional<int> > {
            co_return co_await std::move(receiver);  
        };
        auto send = [&]() -> Task<void> {
            EXPECT_TRUE(sender.send(42));            
            co_return;
        };
        auto [a, b] = co_await whenAll(recv(), send());
        EXPECT_EQ(a, 42);
    }
    { // blocking close
        auto [sender, receiver] = oneshot::channel<int>();
        auto recv = [&]() -> Task<std::optional<int> > {
            co_return co_await std::move(receiver);  
        };
        auto send = [&]() -> Task<void> {
            sender.close();
            co_return;
        };
        auto [a, b] = co_await whenAll(recv(), send());
        EXPECT_EQ(a, std::nullopt);
    }
    {
        // cancel
        auto [sender, receiver] = oneshot::channel<int>();
        auto recv = [&]() -> Task<std::optional<int> > {
            co_return co_await std::move(receiver);  
        };
        auto handle = spawn(recv());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
    {
        // unique_ptr, some moveonly types
        auto [sender, receiver] = oneshot::channel<std::unique_ptr<int> >();
        EXPECT_TRUE(sender.send(std::make_unique<int>(42)));
        auto res = co_await std::move(receiver);
        auto ptr = std::move(*res);
        EXPECT_EQ(*ptr, 42);
    }

    // Cross thread, we submit it to threadpool
    {
        auto [sender, receiver] = oneshot::channel<int>();
        auto handle = spawnBlocking([&]() mutable { // In threadpool
            auto res = sender.send(42); 
            EXPECT_TRUE(res);
        });
        EXPECT_EQ(co_await std::move(receiver), 42);
        EXPECT_TRUE(co_await std::move(handle));
    }

    {
        auto [sender, receiver] = oneshot::channel<int>();
        auto handle = spawnBlocking([&]() mutable {
            auto res = receiver.blockingRecv(); 
            EXPECT_EQ(res, 42);
        });
        co_await sleep(10ms); // wait for the thread to start
        EXPECT_TRUE(sender.send(42));
        EXPECT_TRUE(co_await std::move(handle));
    }

    {
        // close
        auto [sender, receiver] = oneshot::channel<int>();
        receiver.close();
        co_await blocking([&]() mutable {
            EXPECT_EQ(sender.send(42), Err(42)); // Failed to send, because the receiver is closed
        });
    }

    {
        // try recv
        auto [sender, receiver] = oneshot::channel<int>();
        EXPECT_TRUE(sender.send(42));
        co_await blocking([&]() mutable {
            EXPECT_EQ(receiver.tryRecv(), 42);
        });
    }

    {
        // try recv with data (sender closed)
        auto [sender, receiver] = oneshot::channel<int>();
        EXPECT_TRUE(sender.send(42));
        sender.close();

        co_await blocking([&]() mutable {
            EXPECT_EQ(receiver.tryRecv(), 42);
        });
    }

    {
        // close after blcoking recv
        auto [sender, receiver] = oneshot::channel<int>();
        auto handle = spawnBlocking([&]() mutable {
            auto res = receiver.blockingRecv();
            EXPECT_EQ(res, std::nullopt); // closed
        });
        co_await sleep(10ms); // wait for the thread to start
        sender.close();

        EXPECT_TRUE(co_await std::move(handle));
    }
}

