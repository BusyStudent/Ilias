#pragma once

#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <memory> // std::shared_ptr
#include <set> // std::set

ILIAS_NS_BEGIN

/**
 * @brief A Scope guard for task execution, it will await all tasks in the scope to finish
 * 
 */
class ILIAS_API TaskScope final {
public:
    ~TaskScope();

    auto size() const noexcept -> size_t;
    auto empty() const noexcept -> bool;

    template <typename Fn>
    [[nodiscard]]
    static auto enter(Fn fn) -> std::invoke_result_t<Fn, TaskScope &>;

    template <typename Fn>
        requires (std::invocable<Fn, TaskScope &>)
    static auto blockingEnter(Fn fn) -> std::invoke_result_t<Fn, TaskScope &>;
private:
    TaskScope(const TaskScope &) = delete;
    TaskScope();

    auto insert(std::shared_ptr<task::TaskSpawnContext> ctxt) -> void;
    auto wait() -> void;

    std::set<
        std::shared_ptr<task::TaskSpawnContext>
    > mTasks;
};

template <typename Fn>
    requires (std::invocable<Fn, TaskScope &>)
inline auto TaskScope::blockingEnter(Fn fn) -> std::invoke_result_t<Fn, TaskScope &> {
    struct Guard {
        TaskScope &scope;
        ~Guard() {
            scope.wait();
        }
    }
    TaskScope scope;
    Guard guard {scope};
    return fn(scope);
}

ILIAS_NS_END