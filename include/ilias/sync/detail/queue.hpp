// INTERNAL!!
#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <list> // std::list

ILIAS_NS_BEGIN

namespace sync {

class WaitQueue;

// The part of helper awaiter for register the caller to the wait queue, user should not use it directly
class [[nodiscard]] AwaiterBase {
public:
    AwaiterBase(WaitQueue &queue) : mQueue(queue) {}
    AwaiterBase(AwaiterBase &&) = default;
    ~AwaiterBase() = default;

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> void;
private:
    auto onWakeupRaw() -> void;
    auto onStopRequested() -> void;

    WaitQueue &mQueue;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
    std::list<AwaiterBase *>::iterator mIt;
    void (*mOnWakeup)(AwaiterBase *self) = nullptr; // Additional wakeup handler for child classes
template <typename T>
friend class WaitAwaiter;
friend class WaitQueue;
};

// The wait queue used to manage, FIFO, can't destroy until all awaiters are notified
class ILIAS_API WaitQueue {
public:
    WaitQueue() noexcept;
    WaitQueue(const WaitQueue &) = delete;
    ~WaitQueue();

    // Wakeup one, no-op on empty queue
    auto wakeupOne() -> void;
    auto wakeupAll() -> void;
    auto operator =(const WaitQueue &) = delete;
private:
    std::list<AwaiterBase *> mAwaiters;
friend class AwaiterBase;
};

template <typename T>
class WaitAwaiter : public AwaiterBase {
public:
    WaitAwaiter(WaitQueue &queue) : AwaiterBase(queue) {
        if constexpr (requires(T &t) { t.onWakeup(); }) { // Check if T has onWakeup
            mOnWakeup = proxy;
        }
    }
private:
    template <char = 0>
    static auto proxy(AwaiterBase *self) -> void {
        static_cast<T *>(self)->onWakeup(); // Call onWakeup
    }
};

} // namespace sync

ILIAS_NS_END