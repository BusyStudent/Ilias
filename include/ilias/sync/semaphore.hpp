#pragma once

#include <ilias/sync/detail/queue.hpp> // WaitQueue, WaitAwaiter
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <optional> // std::optional

ILIAS_NS_BEGIN

class Semaphore;
class SemaphorePermit {
public:
    SemaphorePermit(SemaphorePermit &&);
    ~SemaphorePermit();
private:
    SemaphorePermit(Semaphore &sem);
    Semaphore *mSem = nullptr;
friend class Semaphore;
};

/**
 * @brief The coroutine version of a semaphore. but not thread-safe
 * 
 */
class Semaphore {
public:
    /**
     * @brief Construct a new Semaphore object with the specified count
     * 
     * @param count The initial count
     */
    explicit Semaphore(size_t count) : mCount(count) {}
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
            auto await_ready() const noexcept -> bool {
                return sem.mCount > 0;
            }
            [[nodiscard]]
            auto await_resume() -> SemaphorePermit {
                return sem.tryAcquire().value();
            }
            Semaphore &sem;
        };
        return Awaiter(*this);
    }

    /**
     * @brief Try to accquire a permit from the semaphore
     * 
     * @return std::optional<SemaphorePermit> 
     */
    [[nodiscard]]
    auto tryAcquire() -> std::optional<SemaphorePermit> {
        if (mCount > 0) {
            mCount -= 1;
            return SemaphorePermit(*this);
        }
        return std::nullopt;
    }

    /**
     * @brief Release a permit to the semaphore
     * @note This method is used as impl, not recommended to use directly
     * 
     */
    auto releaseRaw() -> void {
        mCount += 1;
        mQueue.wakeupOne();
    }

    /**
     * @brief Get the number of available permits
     * 
     * @return size_t 
     */
    auto available() const noexcept -> size_t {
        return mCount;
    }
private:
    sync::WaitQueue mQueue;
    size_t mCount;
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

ILIAS_NS_END