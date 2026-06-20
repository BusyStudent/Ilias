#include <ilias/sync/mpsc.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>

using namespace ilias;
using namespace std::literals;

ILIAS_TEST(Sync, Mpsc) {
    {
        auto [sender, receiver] = mpsc::channel<int>(10);
        EXPECT_TRUE(co_await sender.send(42));
        EXPECT_EQ(co_await receiver.recv(), 42);
    }
    { // close
        auto [sender, receiver] = mpsc::channel<int>(10);
        sender.close();
        EXPECT_FALSE(co_await receiver.recv());
        EXPECT_TRUE(sender.isClosed());
        EXPECT_TRUE(receiver.isClosed());
    }
    {
        auto [sender, receiver] = mpsc::channel<int>(10);
        receiver.close();
        EXPECT_FALSE(co_await sender.send(42));
        EXPECT_TRUE(sender.isClosed());
        EXPECT_TRUE(receiver.isClosed());
    }
    { // blocking
        auto [sender, receiver] = mpsc::channel<int>(10);
        auto recvWorker = [](auto receiver) -> Task<void> {
            for (int i = 0; i < 100; i++) {
                EXPECT_EQ(co_await receiver.recv(), i);
            }  
            receiver.close();
        };
        auto handle = spawn(recvWorker(std::move(receiver)));
        for (int i = 0; i < 100; i++) {
            EXPECT_TRUE(co_await sender.send(i));
        }
        co_await this_coro::yield();
        EXPECT_FALSE(co_await sender.send(100)); // closed
        EXPECT_FALSE(co_await sender.send(101)); // closed
        EXPECT_TRUE(co_await std::move(handle)); // wait for the recvWorker to finish
    }
    {
        auto [sender, receiver] = mpsc::channel<int>(10);
        auto sendWorker = [](auto sender) -> Task<void> {
            for (int i = 0; i < 10; i++) {
                EXPECT_TRUE(co_await sender.send(i));
            }  
        };
        auto group = TaskGroup<void>();
        for (int i = 0; i < 10; i++) {
            group.spawn(sendWorker(sender));
        }
        for (int i = 0; i < 100; i++) {
            EXPECT_TRUE(co_await receiver.recv());
        }
        auto _ = co_await group.waitAll();
    }
    {
        // cancel
        auto [sender, receiver] = mpsc::channel<int>(10);
        auto recv = [&]() -> Task<void> {
            co_await receiver.recv();
            ILIAS_TRAP(); // should not reach here
        };
        auto handle = spawn(recv());
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
    {
        // move ony types
        auto [sender, receiver] = mpsc::channel<std::unique_ptr<int> >(10);
        EXPECT_TRUE(co_await sender.send(std::make_unique<int>(42)));
        auto res = co_await receiver.recv();
        EXPECT_TRUE(res);
        EXPECT_EQ(**res, 42);
    }
    {
        // try recv
        auto [sender, receiver] = mpsc::channel<int>(1);
        EXPECT_EQ(receiver.tryRecv(), Err(mpsc::TryRecvError::Empty));

        // send an value
        EXPECT_TRUE(co_await sender.send(42));
        EXPECT_EQ(receiver.tryRecv(), 42);

        // closed
        sender.close();
        EXPECT_EQ(receiver.tryRecv(), Err(mpsc::TryRecvError::Closed));
    }

    {
        // try recv with data (sender closed)
        auto [sender, receiver] = mpsc::channel<int>(1);
        EXPECT_TRUE(co_await sender.send(42));
        sender.close();

        EXPECT_EQ(receiver.tryRecv(), 42);
    }

    {
        // try send
        auto [sender, receiver] = mpsc::channel<int>(1);
        EXPECT_TRUE(sender.trySend(42));
        EXPECT_EQ(sender.trySend(42).error().reason, mpsc::TrySendError::Full);

        EXPECT_EQ(co_await receiver.recv(), 42);
        receiver.close();

        EXPECT_EQ(sender.trySend(42).error().reason, mpsc::TrySendError::Closed);
    }

    {
        // permit
        auto [sender, receiver] = mpsc::channel<int>(1);
        auto permit = sender.tryReserve();
        EXPECT_TRUE(permit);
        permit->send(42);

        // Then it should be full
        EXPECT_EQ(sender.tryReserve(), Err(mpsc::TrySendError::Full));

        // Get the value
        EXPECT_EQ(co_await receiver.recv(), 42);

        // reserve & give the slot back
        EXPECT_TRUE(co_await sender.reserve());

        receiver.close();
        EXPECT_EQ(sender.tryReserve(), Err(mpsc::TrySendError::Closed));
    }

    // Try Blocking Send, Cross the thread
    {
        auto [sender, receiver] = mpsc::channel<int>(10);
        auto thread = std::thread([sender]() mutable {
            for (int i = 0; i < 100; i++) {
                EXPECT_TRUE(sender.blockingSend(i));
            }
        });
        // In current thread, we receive the data
        for (int i = 0; i < 100; i++) {
            EXPECT_EQ(co_await receiver.recv(), i);
        }
        receiver.close();
        thread.join();
    }

    {
        // Cross thread
        auto [sender, receiver] = mpsc::channel<int>(10);
        auto exec = useExecutor<EventLoop>();
        auto thread1 = Thread(exec, [sender]() mutable -> Task<void> {
            for (int i = 0; i < 100; i++) {
                EXPECT_TRUE(co_await sender.send(i));
            }
            sender.close();
        });
        auto thread2 = std::thread([receiver = std::move(receiver)]() mutable {
            for (int i = 0; i < 200; i++) {
                EXPECT_TRUE(receiver.blockingRecv());
            }
            EXPECT_FALSE(receiver.blockingRecv()); // Closed
        });
        for (int i = 0; i < 100; i++) {
            EXPECT_TRUE(co_await sender.send(i));
        }
        sender.close();
        
        // Wait
        co_await thread1.join();
        thread2.join();
    }
}
