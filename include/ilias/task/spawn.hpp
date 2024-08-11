#pragma once

#include <ilias/task/detail/view.hpp>
#include <ilias/task/task.hpp>
#include <memory>

ILIAS_NS_BEGIN

// TODO: 
namespace detail {

struct SpawnControlBlock {
    TaskView<> mView;
};

template <typename T>
struct SpawnBlock : public SpawnControlBlock {
    Task<T> mTask;
};

}


class CancelHandle {
private:
    std::shared_ptr<detail::SpawnControlBlock> mControlBlock;
};

template <typename T>
class WaitHandle {
public:
    explicit WaitHandle(std::shared_ptr<detail::SpawnControlBlock> controlBlock) : 
        mControlBlock(controlBlock) { }

    WaitHandle() = default;
    WaitHandle(const WaitHandle &) = delete;
    WaitHandle(WaitHandle &&) = default;

    auto operator =(const WaitHandle &) = delete;
    auto operator =(WaitHandle &&) = default;
private:
    std::shared_ptr<detail::SpawnControlBlock> mControlBlock;
};


ILIAS_NS_END