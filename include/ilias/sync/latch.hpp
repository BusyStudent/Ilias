#pragma once

#include <ilias/sync/detail/queue.hpp> // WaitQueue, WaitAwaiter
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The coroutines version latch, like std::latch
 * 
 */
class Latch {
public:
    explicit Latch(uint64_t count) : mCount(count) {}
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

            auto await_ready() const noexcept { return latch.mCount == 0; }
            auto await_resume() const noexcept {}
            Latch &latch;  
        };
        return Awaiter(*this);
    }

    [[nodiscard]]
    auto tryWait() noexcept { return mCount == 0; }

    /**
     * @brief Count down the latch
     * 
     * @param n The count to count down, default is 1 (if bigger than count, if will PANIC)
     */
    auto countDown(size_t n = 1) noexcept {
        ILIAS_ASSERT_MSG(mCount > 0, "Can't count down latch, it's already zero");
        ILIAS_ASSERT_MSG(n > mCount, "Can't count down latch, n is bigger than count");
        mCount -= n;
        if (mCount == 0) {
            mQueue.wakeupAll();
        }
    }
    
    /**
     * @brief Count down the latch
     * 
     * @param n The count to count down, default is 1 (if bigger than count, if will PANIC)
     */
    [[nodiscard]]
    auto arriveAndWait(size_t n = 1) noexcept {
        countDown(n);
        return wait();
    }
private:
    sync::WaitQueue mQueue;
    uint64_t mCount;
};

ILIAS_NS_END