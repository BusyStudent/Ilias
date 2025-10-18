#pragma once

#include <ilias/sync/detail/queue.hpp> // WaitQueue, WaitAwaiter
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <atomic>

ILIAS_NS_BEGIN

/**
 * @brief The coroutines version latch, like std::latch
 * 
 */
class Latch {
public:
    explicit Latch(ptrdiff_t count) : mCount(count) { ILIAS_ASSERT(count >= 0); }
    Latch(const Latch &) = delete;
    ~Latch() = default;

    /**
     * @brief Wait until the latch is counted down to zero
     * 
     * @return don't forget to use co_await
     */
    [[nodiscard]]
    auto wait() noexcept {
        struct Awaiter : public sync::WaitAwaiter<Awaiter> {
            Awaiter(Latch &l) : sync::WaitAwaiter<Awaiter>(l.mQueue), latch(l) {}

            auto await_ready() const noexcept { return latch.tryWait(); }
            auto await_resume() const noexcept {}
            auto onWakeup() const noexcept { return latch.tryWait(); }
            Latch &latch;
        };
        return Awaiter(*this);
    }

    /**
     * @brief Blocking wait until the latch is counted down to zero
     * 
     * @note It will ```BLOCK``` the current thread
     */
    [[nodiscard]]
    auto blockingWait() noexcept {
        return mQueue.blockingWait([this]() { return tryWait(); });
    }

    [[nodiscard]]
    auto tryWait() const noexcept -> bool { return mCount.load(std::memory_order_acquire) == 0; }

    /**
     * @brief Count down the latch
     * 
     * @param n The count to count down, default is 1 (if bigger than count or negative or 0, if will ```CRASH```)
     */
    auto countDown(ptrdiff_t n = 1) noexcept {
        auto count = mCount.fetch_sub(n, std::memory_order_acq_rel);
        ILIAS_ASSERT_MSG(n >= 0, "Can't count down latch, n is negative");
        ILIAS_ASSERT_MSG((count - n) >= 0, "Can't count down latch, n is bigger than count");
        if (count == n) { // Now it's zero, wakeup all
            mQueue.wakeupAll();
        }
    }
    
    /**
     * @brief Count down the latch
     * 
     * @param n The count to count down, default is 1 (if bigger than count or negative or 0, if will ```CRASH```)
     */
    [[nodiscard]]
    auto arriveAndWait(ptrdiff_t n = 1) noexcept {
        countDown(n);
        return wait();
    }
private:
    sync::WaitQueue        mQueue;
    std::atomic<ptrdiff_t> mCount;
};

ILIAS_NS_END