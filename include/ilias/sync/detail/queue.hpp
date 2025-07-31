// INTERNAL!!
#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <list> // std::list

ILIAS_NS_BEGIN

namespace sync {

class WaitQueue;

// The part of helper awaiter for register the caller to the wait queue
class [[nodiscard]] AwaiterBase {
public:
    AwaiterBase(WaitQueue &queue) : mQueue(queue) {}
    AwaiterBase(AwaiterBase &&) = default;

    ILIAS_API
    auto await_suspend(runtime::CoroHandle caller) -> void;
private:
    auto onNotify() -> void;
    auto onStopRequested() -> void;

    WaitQueue &mQueue;
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
    std::list<AwaiterBase *>::iterator mIt;
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
    // no-op here, reserve for future
    using AwaiterBase::AwaiterBase;
};

} // namespace sync

ILIAS_NS_END