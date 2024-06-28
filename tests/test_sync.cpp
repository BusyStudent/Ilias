#pragma once

#include "../include/ilias_mutex.hpp"
#include "../include/ilias_loop.hpp"
#include "../include/ilias_scope.hpp"
#include <gtest/gtest.h>


using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(TestMutex, Lock) {
    Mutex m;
    TaskScope scope;

    scope.spawn([&]() -> Task<> {
        auto ok = co_await m.lock();
        if (ok) {
            m.unlock();
        }

        auto guard = co_await m.lockGuard();
        co_return {};
    });
    scope.syncWait();

    std::vector<int> arrived;
    enum {
        A, B, C
    };

    scope.spawn([&]() -> Task<> {
        co_await Sleep(100ms);
        auto guard = co_await m.lockGuard();
        std::cout << "Task A" << std::endl;
        arrived.push_back(A);
        co_return {};
    });
    scope.spawn([&]() -> Task<> {
        co_await Sleep(10ms);
        auto guard = co_await m.lockGuard();
        std::cout << "Task B" << std::endl;
        arrived.push_back(B);
        co_await Sleep(10ms);
        co_return {};
    });
    scope.spawn([&]() -> Task<> {
        co_await Sleep(20ms);
        auto guard = co_await m.lockGuard();
        std::cout << "Task C" << std::endl;
        arrived.push_back(C);
        co_return {};
    });
    scope.syncWait();

    // Must b first arrived
    ASSERT_TRUE(arrived.size() == 3);
    ASSERT_TRUE(arrived[0] == B);
    ASSERT_TRUE(arrived[1] == C);
    ASSERT_TRUE(arrived[2] == A);
}


TEST(TestMutex, Ill) {
    EXPECT_DEATH_IF_SUPPORTED({
        Mutex m;
        m.tryLock(); //< forget to unlock the mutex
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        Mutex m;
        m.unlock(); //< unlock the mutex that is not locked
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        Mutex m;
        m.lock(); //< forget to use co_await
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        Mutex m;
        m.lockGuard(); //< forget to use co_await
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        Mutex m;
        ilias_wait [&]() -> Task<> {
            co_await m.lock(); //< forget to unlock
            co_return {};
        }();
    }, "");
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    MiniEventLoop loop;
    return RUN_ALL_TESTS();
}