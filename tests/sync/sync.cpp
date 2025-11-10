#include <ilias/sync/mutex.hpp>
#include <ilias/sync/event.hpp>
#include <ilias/sync/latch.hpp>
#include <ilias/sync/semaphore.hpp>
#include <ilias/sync/oneshot.hpp>
#include <ilias/sync/mpsc.hpp>
#include <ilias/task.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <latch>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

// Mutex
ILIAS_TEST(Sync, BasicMutexLockUnlock) {
    Mutex mtx;
    EXPECT_FALSE(mtx.isLocked());

    auto lock = co_await mtx.lock();
    EXPECT_TRUE(mtx.isLocked());

    lock.unlock();
    EXPECT_FALSE(mtx.isLocked());
}

ILIAS_TEST(Sync, MutexMultipleWaiters) {
    Mutex mtx;
    int shared = 0;

    auto taskA = [&]() -> Task<int> {
        auto lock = co_await mtx.lock();
        shared += 1;
        co_await sleep(10ms);
        co_return shared;
    };

    auto taskB = [&]() -> Task<int> {
        auto lock = co_await mtx.lock();
        shared += 2;
        co_return shared;
    };

    auto taskC = [&]() -> Task<int> {
        co_await sleep(20ms);
        auto lock = co_await mtx.lock();
        shared += 3;
        co_return shared;
    };

    auto [a, b, c] = co_await whenAll(taskA(), taskB(), taskC());
    EXPECT_EQ(shared, 6);
}

ILIAS_TEST(Sync, MutexCancel) {
    Mutex mtx;

    auto lock = co_await mtx.lock();
    auto taskA = [&]() -> Task<void> {
        auto _ = co_await mtx.lock();
        ILIAS_TRAP(); // never reached
    };
    auto handle = spawn(taskA());
    handle.stop();
    EXPECT_FALSE(co_await std::move(handle));
}

ILIAS_TEST(Sync, MutexCrossThread) {
    Mutex mtx;
    int value = 0;

    auto latch = std::latch {3};
    auto exec = useExecutor<EventLoop>();
    auto callable = [&]() -> Task<void> {
        latch.arrive_and_wait();
        for (int i = 0; i < 100000; ++i) {
            auto lock = co_await mtx.lock();
            value += 1;
        }
    };
    auto callable2 = [&]() -> void {
        latch.arrive_and_wait();
        for (int i = 0; i < 100000; ++i) {
            auto lock = mtx.blockingLock();
            value += 1;
        }
    };

    auto thread = Thread(exec, callable);
    auto thread2 = std::thread(callable2);
    
    latch.arrive_and_wait();
    for (int i = 0; i < 100000; ++i) {
        auto lock = co_await mtx.lock();
        value += 1;
    }

    co_await thread.join();
    thread2.join();
    EXPECT_EQ(value, 300000);
}

// Latch
ILIAS_TEST(Sync, Latch) {
    Latch latch {3};
    auto fn = [&]() -> Task<void> {
        co_await latch.arriveAndWait();
    };
    auto _ = co_await whenAll(fn(), fn(), fn());
    EXPECT_TRUE(latch.tryWait()); // count is 0
}

// Locked
ILIAS_TEST(Sync, Locked) {
    Locked<int> value {10};
    EXPECT_FALSE(value.isLocked());
    {
        auto ptr = value.tryLock();
        EXPECT_TRUE(ptr);
        EXPECT_TRUE(value.isLocked());
        EXPECT_EQ(*ptr->get(), 10);
        **ptr = 114514;
    }
    EXPECT_FALSE(value.isLocked());
    {
        auto ptr = co_await value;
        EXPECT_EQ(*ptr, 114514);
    }
    co_return;
}

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

auto main(int argc, char **argv) -> int {
    EventLoop loop;
    loop.install();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}