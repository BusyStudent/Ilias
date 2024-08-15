#pragma once

#include <ilias/task/detail/view.hpp>
#include <ilias/task/task.hpp>
#include <memory>

ILIAS_NS_BEGIN

// TODO: Finish this
namespace detail {

struct SpawnData {
    TaskView<> mView;
    std::coroutine_handle<> mHandle;
};

template <typename T>
struct SpawnDataT final : public SpawnData {
public:
    Task<T> mTask;
};

}

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

    auto done() const -> bool { return mData->mView.done(); }
    auto cancel() const -> void { return mData->mView.cancel(); }
    auto operator =(const CancelHandle &) -> CancelHandle & = default;
private:
    std::shared_ptr<detail::SpawnData> mData;
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
    explicit WaitHandle(std::shared_ptr<detail::SpawnData> controlBlock) : 
        mData(controlBlock) { }

    WaitHandle() = default;
    WaitHandle(const WaitHandle &) = delete;
    WaitHandle(WaitHandle &&) = default;

    auto done() const -> bool { return mData->mView.done(); }
    auto cancel() const -> void { return mData->mView.cancel(); }

    auto operator =(const WaitHandle &) = delete;
    auto operator =(WaitHandle &&) = default;
private:
    std::shared_ptr<detail::SpawnData> mData;
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
    auto controlBlock = std::make_shared<detail::SpawnDataT<T>>();
    controlBlock->mTask = std::move(task);
    controlBlock->mView = controlBlock->mTask.view();
    return WaitHandle<T>(controlBlock);
}


ILIAS_NS_END