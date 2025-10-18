#pragma once

#include <ilias/sync/detail/queue.hpp> // WaitQueue, WaitAwaiter
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <optional> // std::optional
#include <atomic> // std::atomic

ILIAS_NS_BEGIN

class Semaphore;
class SemaphorePermit {
public:
    SemaphorePermit(SemaphorePermit &&);
    ~SemaphorePermit();

    auto leak() noexcept -> void;
private:
    SemaphorePermit(Semaphore &sem);
    Semaphore *mSem = nullptr;
friend class Semaphore;
};

/**
 * @brief The coroutine version of a semaphore. thread-safe, like std::semaphore
 * 
 */
class Semaphore {
public:
    /**
     * @brief Construct a new Semaphore object with the specified count
     * 
     * @param count The initial count
     */
    explicit Semaphore(ptrdiff_t count) : mCount(count) { ILIAS_ASSERT(count >= 0); }
    Semaphore(const Semaphore &) = delete;

    /**
     * @brief Accquire a permit from the semaphore
     * 
     * @return SemaphorePermit
     */
    [[nodiscard]]
    auto acquire() {
        struct Awaiter : sync::WaitAwaiter<Awaiter> {
            Awaiter(Semaphore &sem) : sync::WaitAwaiter<Awaiter>(sem.mQueue), sem(sem) {}

            auto await_ready() -> bool {
                return sem.tryAcquireInternal();
            }

            [[nodiscard]]
            auto await_resume() -> SemaphorePermit {
                return SemaphorePermit(sem); // Got this in onWakeup
            }

            auto onWakeup() -> bool {
                return sem.tryAcquireInternal();
            }

            Semaphore &sem;
        };
        return Awaiter(*this);
    }

    /**
     * @brief Blocking accquire a permit from the semaphore
     * @note It will ```BLOCK``` the current thread
     * 
     * @return SemaphorePermit 
     */
    [[nodiscard]]
    auto blockingAcquire() -> SemaphorePermit {
        mQueue.blockingWait([&]() { return tryAcquireInternal(); });
        return SemaphorePermit(*this);
    }

    /**
     * @brief Try to accquire a permit from the semaphore
     * 
     * @return std::optional<SemaphorePermit> 
     */
    [[nodiscard]]
    auto tryAcquire() -> std::optional<SemaphorePermit> {
        auto current = mCount.load(std::memory_order_acquire);
        while (current > 0) { // Still has permits
            if (mCount.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel)) {
                return SemaphorePermit(*this);
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Release a permit to the semaphore
     * @note This method is used as impl, not recommended to use directly
     * 
     */
    auto releaseRaw() -> void {
        mCount.fetch_add(1, std::memory_order_release);
        mQueue.wakeupOne();
    }

    /**
     * @brief Increase the number of available permits
     * 
     * @param n The number of permits to add (it can't be negative)
     */
    auto addPremits(ptrdiff_t n) -> void {
        ILIAS_ASSERT(n >= 0);
        mCount.fetch_add(n, std::memory_order_release);
        for (ptrdiff_t i = 0; i < n; ++i) {
            mQueue.wakeupOne();
        }
    }

    /**
     * @brief Get the number of available permits
     * 
     * @return ptrdiff_t 
     */
    auto available() const noexcept -> ptrdiff_t {
        return mCount.load(std::memory_order_acquire);
    }
private:
    auto tryAcquireInternal() -> bool {
        auto premit = tryAcquire();
        if (premit) {
            premit->leak(); // Transfer the permit to waiter
            return true;
        }
        return false;
    }

    sync::WaitQueue        mQueue;
    std::atomic<ptrdiff_t> mCount;
};

inline SemaphorePermit::SemaphorePermit(Semaphore &sem) : mSem(&sem) {}
inline SemaphorePermit::SemaphorePermit(SemaphorePermit &&other) : mSem(other.mSem) {
    other.mSem = nullptr;
}
inline SemaphorePermit::~SemaphorePermit() {
    if (mSem) {
        mSem->releaseRaw();
    }
}
inline auto SemaphorePermit::leak() noexcept -> void {
    mSem = nullptr;
}

ILIAS_NS_END