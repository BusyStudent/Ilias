/**
 * @file uring_core.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Basic utils for io_uring
 * @version 0.1
 * @date 2024-09-26
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <liburing.h>

ILIAS_NS_BEGIN

namespace linux {

/**
 * @brief The Callback of the io_uring, store in the cqe/sqe's user_data
 * 
 */
class UringCallback {
public:
    /**
     * @brief The callback, it will be invoke when the cqe completed, (it is safe to set it to nullptr (no-op))
     * 
     */
    void (*onCallback)(UringCallback *self, const ::io_uring_cqe &cqe) = nullptr;

    /**
     * @brief Get the helper UringCallback (for do-nothing)
     * 
     * @return UringCallback* 
     */
    static auto noop() -> UringCallback * {
        static UringCallback instance {
            .onCallback = [](UringCallback *, const ::io_uring_cqe &cqe) {
                ILIAS_TRACE("Uring","noop res: {}, flags: {}", cqe.res, cqe.flags);
            },
        };
        return &instance;
    }
};

// The callback used to submit the request
class UringCallbackIo : public UringCallback {};

// The callback used to submit the cancel request
class UringCallbackCancel : public UringCallback {};

/**
 * @brief Generic Uring Submit code
 * 
 */
class UringAwaiterBase : private UringCallbackIo, private UringCallbackCancel {
public:
    UringAwaiterBase(::io_uring &ring) : mRing(ring) {

    }

    auto await_ready() -> bool {
        mSqe = allocSqe();
        return false;
    }

    auto await_suspend(runtime::CoroHandle caller) -> void {
        mCaller = caller;

        // Set the callback
        ::io_uring_sqe_set_data(mSqe, static_cast<UringCallbackIo*>(this));
        UringCallbackIo::onCallback = UringAwaiterBase::callback;

        mReg.register_<&UringAwaiterBase::onStopRequested>(caller.stopToken(), this);
    }

    auto sqe() const -> ::io_uring_sqe * {
        return mSqe;
    }
private:
    auto allocSqe() -> ::io_uring_sqe * {
        auto sqe = ::io_uring_get_sqe(&mRing);
        if (!sqe) {
            auto n = ::io_uring_submit(&mRing);
            sqe = ::io_uring_get_sqe(&mRing);
        }
        ILIAS_ASSERT(sqe);
        return sqe;
    }

    auto onStopRequested() -> void {
        ILIAS_TRACE("Uring", "Operation cancel request");
        mCancelSqe = allocSqe();
        UringCallbackCancel::onCallback = UringAwaiterBase::cancelCallback;
        ::io_uring_prep_cancel(mCancelSqe, this, 0);
        ::io_uring_sqe_set_data(mCancelSqe, static_cast<UringCallbackCancel*>(this));
    }

    auto onResume() {
        if (mResult == -ECANCELED && mCaller.isStopRequested()) {
            mCaller.setStopped();
            return;
        }
        mCaller.resume();
    }

    static auto callback(UringCallback *_self, const ::io_uring_cqe &cqe) -> void {
        ILIAS_TRACE("Uring", "Operation completed, res: {}, flags: {}, err: {}", cqe.res, cqe.flags, err2str(cqe.res));
        auto self = static_cast<UringAwaiterBase*>(static_cast<UringCallbackIo*>(_self));
        self->mResult = cqe.res;
        self->mSqe = nullptr; //< Mark done
        if (self->mCancelSqe) { //< Cancel is not done, wait it
            ILIAS_TRACE("Uring", "Cancel is not done, wait for it");
            return;
        }
        self->onResume();
    }

    static auto cancelCallback(UringCallback *_self, const ::io_uring_cqe &cqe) -> void {
        ILIAS_TRACE("Uring", "Operation cancel completed, res: {}, flags: {}, err: {}", cqe.res, cqe.flags, err2str(cqe.res));
        auto self = static_cast<UringAwaiterBase*>(static_cast<UringCallbackCancel*>(_self));
        self->mCancelSqe = nullptr;
        if (self->mSqe) { //< The main request not done, wait it
            ILIAS_TRACE("Uring", "Main request not done, wait for it");
            return;
        }
        self->onResume();
    }

    static auto err2str(int64_t res) -> const char * {
        return res < 0 ? ::strerror(-res) : "OK";
    }

    ::io_uring     &mRing; //< Which Ring
    ::io_uring_sqe *mSqe = nullptr; //< The sqe we used to submit
    ::io_uring_sqe *mCancelSqe = nullptr; //< The sqe used to submit cancel
    int64_t         mResult = 0; //< The result of the completion
    runtime::CoroHandle mCaller;
    runtime::StopRegistration mReg;
template <typename T>
friend class UringAwaiter;
};

/**
 * @brief The template of all Uring Awaiter
 * 
 * @tparam T 
 */
template <typename T>
class UringAwaiter : public UringAwaiterBase {
public:
    using UringAwaiterBase::UringAwaiterBase;

    auto await_suspend(runtime::CoroHandle caller) -> void {
        static_cast<T*>(this)->onSubmit();
        UringAwaiterBase::await_suspend(caller);
    }

    auto await_resume() {
        auto self = static_cast<T*>(this);
        return self->onComplete(mResult);
    }
};

/**
 * @brief wrapping io_uring_prep_timeout
 * 
 */
class UringTimeoutAwaiter final : public UringAwaiter<UringTimeoutAwaiter> {
public:
    UringTimeoutAwaiter(::io_uring &ring, const ::__kernel_timespec &spec) : UringAwaiter(ring), mSpec(spec) {

    }

    auto onSubmit() {
        ILIAS_TRACE("Uring", "Prep timeout for s: {}, ns: {}", mSpec.tv_sec, mSpec.tv_nsec);
        ::io_uring_prep_timeout(sqe(), &mSpec, 0, 0);
    }

    auto onComplete(int64_t result) -> void {
        return;
    }
private:
    ::__kernel_timespec mSpec;
};


} // namespace detail

ILIAS_NS_END