/**
 * @file mini_executor.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provides a simple executor implementation. It is not recommended to use this executor in production code.
 * @version 0.1
 * @date 2024-08-09
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/task/executor.hpp>
#include <ilias/cancellation_token.hpp>
#include <condition_variable>
#include <mutex>
#include <queue>

ILIAS_NS_BEGIN

/**
 * @brief The Minimal Executor, use it as testing case.
 * 
 */
class MiniExecutor final : public Executor {
public:
    MiniExecutor() = default;

    auto post(void (*fn)(void *), void *arg) -> void override {
        std::lock_guard locker(mMutex);
        mQueue.emplace(fn, arg);
        mCond.notify_one();
    }

    auto run(CancellationToken &token) -> void override {
        auto reg = token.register_([&]() {
            mCond.notify_one();
        });
        while (!token.isCancelled()) {
            std::unique_lock locker(mMutex);
            mCond.wait(locker, [&]() {
                return !mQueue.empty() || token.isCancelled();
            });
            if (token.isCancelled()) {
                return;
            }
            auto fn = mQueue.front();
            mQueue.pop();
            locker.unlock();
            fn.first(fn.second);
        }
    }
private:
    std::queue<
        std::pair<void (*)(void *), void *>
    > mQueue;
    std::condition_variable mCond;
    std::mutex mMutex;
};

ILIAS_NS_END