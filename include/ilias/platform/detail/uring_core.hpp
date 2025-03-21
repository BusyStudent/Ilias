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

namespace detail {

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

/**
 * @brief The uring callback, but add one user for store this or extra info
 * 
 */
class UringCallbackEx : public UringCallback {
public:
    union {
        void *ptr;
        int   fd;
    };
};

/**
 * @brief Generic Uring Submit / Compelete Template
 * 
 * @tparam T 
 */
template <typename T>
class UringAwaiter : public UringCallback {
public:
    UringAwaiter(::io_uring &ring) : mRing(ring) {

    }

    auto await_ready() -> bool {
        mSqe = allocSqe();
        return false;
    }

    auto await_suspend(TaskView<> caller) -> void {
        mCaller = caller;
        mReg = caller.cancellationToken().register_(onCancel, this);

        // Submit
        static_cast<T*>(this)->onSubmit();

        // Set the callback
        ::io_uring_sqe_set_data(mSqe, static_cast<UringCallback*>(this));
        UringCallback::onCallback = UringAwaiter::callback;
    }

    auto await_resume() {
        auto self = static_cast<T*>(this);
        return self->onComplete(mResult);
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

    static auto callback(UringCallback *_self, const ::io_uring_cqe &cqe) -> void {
        ILIAS_TRACE("Uring", "Operation completed, res: {}, flags: {}, err: {}", cqe.res, cqe.flags, err2str(cqe.res));
        auto self = static_cast<T*>(_self);
        self->mResult = cqe.res;
        self->mSqe = nullptr; //< Mark done
        if (self->mCancelSqe) { //< Cancel is not done, wait it
            ILIAS_TRACE("Uring", "Cancel is not done, wait for it");
            return;
        }
        self->mCaller.resume();
    }

    static auto cancelCallback(UringCallback *_self, const ::io_uring_cqe &cqe) -> void {
        ILIAS_TRACE("Uring", "Operation cancel completed, res: {}, flags: {}, err: {}", cqe.res, cqe.flags, err2str(cqe.res));
        auto self = static_cast<T*>(static_cast<UringCallbackEx*>(_self)->ptr);
        self->mCancelSqe = nullptr;
        if (self->mSqe) { //< The main request not done, wait it
            ILIAS_TRACE("Uring", "Main request not done, wait for it");
            return;
        }
        self->mCaller.resume();
    }

    static auto onCancel(void *_self) -> void {
        ILIAS_TRACE("Uring", "Operation cancel request");
        auto self = static_cast<T*>(_self);
        
        self->mCancelSqe = self->allocSqe();
        self->mCallbackEx.onCallback = cancelCallback;
        self->mCallbackEx.ptr = self;
        ::io_uring_prep_cancel(self->mCancelSqe, self, 0);
        ::io_uring_sqe_set_data(self->mCancelSqe, &self->mCallbackEx);
    }

    static auto err2str(int64_t res) -> const char * {
        return res < 0 ? ::strerror(-res) : "OK";
    }

    ::io_uring     &mRing; //< Which Ring
    ::io_uring_sqe *mSqe = nullptr; //< The sqe we used to submit
    ::io_uring_sqe *mCancelSqe = nullptr; //< The sqe used to submit cancel
    int64_t         mResult = 0; //< The result of the completion
    TaskView<>      mCaller;
    UringCallbackEx mCallbackEx;
    CancellationToken::Registration mReg;
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

    auto onComplete(int64_t result) -> Result<void> {
        if (result < 0 && result != -ETIME) { // Treat ETIME as success
            return Unexpected(SystemError(-result));
        }
        return {};
    }
private:
    ::__kernel_timespec mSpec;
};


} // namespace detail

ILIAS_NS_END