#pragma once

#include <ilias/detail/intrusive.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/task/spawn.hpp>
#include <ilias/task/task.hpp>
#include <optional>
#include <memory>

ILIAS_NS_BEGIN

/**
 * @brief Scope guard for managing task lifetimes
 *
 * @note Ensure all tasks have completed before destroying the scope;
 *       otherwise the program will `ABORT`. Use waitAll() to wait for
 *       completion, or use enter() to run tasks within the scope.
 */
class ILIAS_API TaskScope final {
public:
    TaskScope();
    TaskScope(const TaskScope &) = delete;
    TaskScope(TaskScope &&) = delete;
    ~TaskScope();

    /**
     * @brief Get the number of running tasks in the scope
     * 
     * @return size_t 
     */
    auto size() const noexcept -> size_t { return mNumRunning; }

    /**
     * @brief Check the scope is empty?
     * 
     * @return true 
     * @return false 
     */
    auto empty() const noexcept -> bool { return mNumRunning == 0; }

    /**
     * @brief Try to stop all tasks in the scope
     * 
     */
    auto stop() noexcept -> void;

    /**
     * @brief Wait all tasks in the scope to finish, (safe on an empty scope)
     * 
     * @return Task<void> 
     */
    auto waitAll() -> Task<void> { return cleanup(std::nullopt); }

    /**
     * @brief Stop all tasks and wait for them to finish.
     * 
     * @return Task<void> 
     */
    auto shutdown() -> Task<void> { stop(); co_return co_await cleanup(std::nullopt); }

    /**
     * @brief Insert an handle to the scope, the scope will wait for it to finish
     * 
     * @tparam T 
     * @param handle The wait handle, the return value will be discard
     * @return StopHandle 
     */
    template <typename T>
    auto insert(WaitHandle<T> handle) -> StopHandle {
        return insertImpl(std::move(handle)._leak());
    }

    // Spawn
    template <typename T>
    auto spawn(Task<T> task, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawn(std::move(task), source));
    }

    template <std::invocable Fn>
    auto spawn(Fn fn, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawn(std::move(fn), source));
    }

    template <std::invocable Fn>
    auto spawnBlocking(Fn fn, runtime::CaptureSource source = {}) -> StopHandle {
        return insert(::ilias::spawnBlocking(std::move(fn), source));
    }

    /**
     * @brief Create a task scope and enter it
     * 
     * @tparam Fn 
     * @param fn The function that return an Task<T>
     * @return std::invoke_result_t<Fn, TaskScope &> 
     */
    template <typename Fn, typename ...Args> requires (std::invocable<Fn, TaskScope &, Args...>)
    [[nodiscard]]
    static auto enter(Fn fn, Args ...args) -> std::invoke_result_t<Fn, TaskScope &, Args...> {
        TaskScope scope;
        co_return co_await ( // Get current context stop token used to forward the stop to the scope
            fn(scope, args...) | finally(scope.cleanup(co_await this_coro::stopToken()))
        );
    }
private:
    auto cleanup(std::optional<runtime::StopToken> token) -> Task<void>; // Use the stop token
    auto insertImpl(intrusive::Rc<task::TaskSpawnContextBase> task) -> StopHandle;
    auto onTaskCompleted(task::TaskSpawnContextBase &ctxt) -> void;

    using List = intrusive::List<task::TaskSpawnContextBase>;

    List   mRunning;
    size_t mNumRunning = 0;
    bool   mStopRequested = false;
    bool   mStopping = false;
    runtime::CoroHandle mWaiter = nullptr;
};

/**
 * @brief A Helper class for manage the async lifetime, note the object must be create by `makeAsyncLifetime` or `make` method
 * 
 * @tparam T 
 */
template <typename T>
class AsyncLifetime {
public:
    // Get the scope
    auto scope() noexcept -> TaskScope & { return mScope; }
    auto scope() const noexcept -> const TaskScope & { return mScope; }

    /**
     * @brief Create the object
     * 
     * @tparam Args
     * @param U used to help the derived class 
     * @param args 
     * @return std::shared_ptr<T> 
     */
    template <typename U = T, typename ...Args>
    static auto make(Args &&...args) -> std::shared_ptr<U> {
        auto ptr = new U(std::forward<Args>(args)...);
        return {ptr, onDestroy<U>};
    }
protected:
    AsyncLifetime() = default; // Make sure to use the factroy method
private:
    template <typename U> // U for derived class
    static auto onDestroy(U *self) -> void {
        spawn([](U *ptr) -> Task<void> {
            auto self = std::unique_ptr<U>(ptr);
            co_await self->mScope.waitAll();
        }(self));
    }
    
    TaskScope mScope;
};

/**
 * @brief Create an shared async lifetime object
 * 
 * @tparam T The type of the object, must be derived from AsyncLifetime<T>
 * @tparam Args 
 */
template <typename T, typename ...Args>
    requires (std::is_base_of_v<AsyncLifetime<T>, T>)
inline auto makeAsyncLifetime(Args &&...args) -> std::shared_ptr<T> {
    return T::template make<T>(std::forward<Args>(args)...);
}

ILIAS_NS_END