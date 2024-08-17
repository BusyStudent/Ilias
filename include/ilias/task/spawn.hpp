#pragma once

#include <ilias/task/detail/view.hpp>
#include <ilias/task/task.hpp>
#include <concepts>

ILIAS_NS_BEGIN

namespace detail {

struct SpawnData {
    SpawnData() {}
    SpawnData(const SpawnData &) = delete;
    ~SpawnData() {
        ILIAS_ASSERT(mTask.isSafeToDestroy());
        mTask.destroy();
    }
    
    auto ref() noexcept {
        ++mRefcount;
    }
    
    auto deref() noexcept {
        if (--mRefcount != 0) {
            return;
        }
        // Do cleanup
        if (mTask.done()) {
            destroy();
            return;
        }
        // Watch when the task done, then destroy
        mTask.registerCallback(compeleteCallback, this);
    }

    auto destroy() -> void {
        if (mDeleteSelf) {
            mDeleteSelf(this);
        }
        else {
            delete this;
        }
    }

    static auto compeleteCallback(void *self) -> void {
        auto executor = static_cast<SpawnData *>(self)->mTask.executor();
        executor->post(destroyCallback, self);
    }

    /**
     * @brief Invoked on the event loop to destroy the self.
     * 
     * @param self 
     */
    static auto destroyCallback(void *self) -> void {
        static_cast<SpawnData *>(self)->destroy();
    }

    TaskView<> mTask;
    uint32_t mRefcount = 0;
    void (*mDeleteSelf)(SpawnData *) = nullptr;
};

template <typename T>
struct SpawnDataWithCallable : SpawnData {
    template <typename ...Args>
    SpawnDataWithCallable(T callable, Args &&...args) : mCallable(std::move(callable)) {
        mDeleteSelf = &deleteSelf;
        mTask = std::invoke(mCallable, std::forward<Args>(args)...)._leak();
    }
    static auto deleteSelf(SpawnData *self) -> void {
        delete static_cast<SpawnDataWithCallable<T> *>(self);
    }
    
    T mCallable;
};

struct SpawnDataRef {
public:
    SpawnDataRef() = default;
    SpawnDataRef(std::nullptr_t) { }
    SpawnDataRef(const SpawnDataRef &other) : mPtr(other.mPtr) {
        ref();
    }
    SpawnDataRef(SpawnDataRef &&other) : mPtr(other.mPtr) {
        other.mPtr = nullptr;
    }
    SpawnDataRef(SpawnData *ptr) : mPtr(ptr) {
        ref();
    }
    ~SpawnDataRef() {
        deref();
    }

    auto operator =(const SpawnDataRef &other) -> SpawnDataRef & {
        deref();
        mPtr = other.mPtr;
        ref();
        return *this;
    }
    auto operator =(SpawnDataRef &&other) -> SpawnDataRef & {
        deref();
        mPtr = other.mPtr;
        other.mPtr = nullptr;
        return *this;
    }
    auto operator =(const std::nullptr_t) -> SpawnDataRef & {
        deref();
        mPtr = nullptr;
        return *this;
    }

    auto operator ->() const noexcept { return mPtr; }

    auto operator <=>(const SpawnDataRef &other) const noexcept = default;
private:
    auto ref() noexcept -> void {
        if (mPtr) {
            mPtr->ref();
        }
    }
    auto deref() noexcept -> void {
        if (mPtr) {
            mPtr->deref();
        }
    }

    SpawnData *mPtr = nullptr;
};

/**
 * @brief Check if callable and args can be used to create a task.
 * 
 * @tparam Callable 
 * @tparam Args 
 */
template <typename Callable, typename ...Args>
concept TaskGenerator = requires(Callable &&callable, Args &&...args) {
    std::invoke(callable, args...)._view();
    std::invoke(callable, args...)._leak();
};

} // namespace detail

/**
 * @brief The handle for a spawned task. used to cancel the task. copyable.
 * 
 */
class CancelHandle {
public:
    CancelHandle() = default;
    CancelHandle(std::nullptr_t) { }
    CancelHandle(const CancelHandle &) = default;
    CancelHandle(CancelHandle &&) = default;

    auto done() const -> bool { return mData->mTask.done(); }
    auto cancel() const -> void { return mData->mTask.cancel(); }
    auto operator =(const CancelHandle &) -> CancelHandle & = default;
    auto operator <=>(const CancelHandle &) const noexcept = default;
private:
    detail::SpawnDataRef mData;
template <typename T>
friend class WaitHandle;
};

/**
 * @brief The handle for a spawned task. used to wait for the task to complete. or cancel, it is unique. and moveon
 * 
 * @tparam T 
 */
template <typename T>
class WaitHandle {
public:
    explicit WaitHandle(const detail::SpawnDataRef &data) : 
        mData(data) { }

    WaitHandle() = default;
    WaitHandle(const WaitHandle &) = delete;
    WaitHandle(WaitHandle &&) = default;

    auto done() const -> bool { return mData->mTask.done(); }
    auto cancel() const -> void { return mData->mTask.cancel(); }

    /**
     * @brief Wait for the task to complete. and return the result.
     * 
     * @return Result<T> 
     */
    auto wait() -> Result<T> {
        if (!done()) {
            // Wait until done
            CancellationToken token;
            mData->mTask.registerCallback(detail::CancelTheTokenHelper, &token);
            mData->mTask.executor()->run(token);
        }
        auto value = static_cast<TaskView<T> &>(mData->mTask).value();
        mData = nullptr;
        return value;
    }

    auto operator =(const WaitHandle &) = delete;
    auto operator =(WaitHandle &&) -> WaitHandle & = default;
private:
    detail::SpawnDataRef mData;
};

/**
 * @brief Spawn a task and return a handle to wait for it to complete.
 * 
 * @tparam T 
 * @param task 
 * @return WaitHandle<T> 
 */
template <typename T>
inline auto spawn(Task<T> &&task) -> WaitHandle<T> {
    auto ref = detail::SpawnDataRef(new detail::SpawnData);
    ref->mTask = task._leak();
    ref->mTask.executor()->schedule(ref->mTask); //< Start it on the event loop
    return WaitHandle<T>(ref);
}

/**
 * @brief Spawn a task from a callable and args  and return a handle to wait for it to complete.
 * 
 * @tparam Callable The invokeable type
 * @tparam Args The arguments to pass to the callable
 * 
 * @param callable The callable to invoke, invoke_result must be a Task<T>
 * @param args The arguments to pass to the callable
 * @return WaitHandle<T>
 */
template <typename Callable, typename ...Args> 
    requires (detail::TaskGenerator<Callable, Args...>)
inline auto spawn(Callable callable, Args &&...args) {
    // Normal function or empty class
    if constexpr (std::is_empty_v<Callable> || 
                  std::is_function_v<Callable> || 
                  std::is_member_function_pointer_v<Callable>) 
    {
        return spawn(std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...));
    }
    else {
        // We need to create a class that hold the callable
        auto ref = detail::SpawnDataRef(
            new detail::SpawnDataWithCallable<Callable>(
                std::forward<Callable>(callable), 
                std::forward<Args>(args)...
            )
        );
        ref->mTask.executor()->schedule(ref->mTask); //< Start it on the event loop
        return WaitHandle<typename std::invoke_result_t<Callable, Args...>::value_type>(ref);
    }
}

ILIAS_NS_END