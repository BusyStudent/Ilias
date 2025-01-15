/**
 * @file generator.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The generator class, provide the coroutine support of yield
 * @version 0.1
 * @date 2025-01-14
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include <ilias/task/detail/promise.hpp>
#include <ilias/task/detail/view.hpp>
#include <ilias/task/detail/wait.hpp>
#include <ilias/task/executor.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <coroutine>

/**
 * @brief The range for for the Generator<T>
 * @note Because for(xxx; xxx; co_await(++it)) compile failed in gcc, so we have to use if instead
 * 
 * @code {.cpp}
 * ilias_foreach(const auto &val, generator()) {
 *  useVal(val);
 * }
 * @endcode
 * 
 * 
 * This macro allows for easy iteration over a generator object.
 * It uses co_await to asynchronously iterate through the generator.
 * 
 * @param var The variable to hold each value from the generator.
 * @param generator The generator object to iterate over.
 */
#define ilias_foreach(var, generator)                                                   \
    if (auto &&_gen_ = (generator); false) { }                                          \
    else if (bool _first_ = true; false) { }                                            \
    else                                                                                \
        for (auto _it_ = co_await _gen_.begin(), _end_ = _gen_.end();; _first_ = false) \
            if (!_first_ ? (co_await (++_it_), 0) : 0; _it_ == _end_) {                 \
                break;                                                                  \
            }                                                                           \
            else                                                                        \
                if (var = *_it_; false) { }                                             \
                else 

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief The awaiter used to execute the generator
 * 
 * @tparam T 
 */
template <typename T>
class GeneratorAwaiter {
public:
    GeneratorAwaiter(GeneratorView<T> view) : mView(view) { }

    auto await_ready() const noexcept -> bool {
        mView.value() = std::nullopt; // Clear the previous value
        mView.resume();
        return mView.done() || bool(mView.value()); // Done? or value yielded
    }

    auto await_suspend(CoroHandle caller) -> void {
        mView.setAwaitingCoroutine(caller);
        mReg = caller.cancellationToken().register_(
            &cancelTheTokenHelper, &mView.cancellationToken()
        );
    }

    auto await_resume() const { 
        mView.rethrowIfException(); 
    }
protected:
    CancellationToken::Registration mReg; //< The reg of we wait for cancel
    GeneratorView<T> mView; //< The generator we execute
};

/**
 * @brief The iterator used to execute the generator
 * 
 * @tparam T 
 */
template <typename T>
class GeneratorIterator {
public:
    GeneratorIterator() = default;
    GeneratorIterator(GeneratorView<T> view, bool end = false) : mView(view), mEnd(end) { }

    auto operator *() const -> T & {
        return *mView.value();
    }

    auto operator ->() const -> T * {
        return &*mView.value();
    }

    /**
     * @brief Try to move to the next
     * 
     * @return GeneratorAwaiter<T> 
     */
    [[nodiscard("Don't not forget to use co_await")]]
    auto operator ++() -> GeneratorAwaiter<T> {
        return mView;
    }

    auto operator ==(const GeneratorIterator &other) const noexcept {
        if (other.mEnd) { // Check is end?
            return mView.done();
        }
        return mView == other.mView;
    }

    auto operator !=(const GeneratorIterator &other) const noexcept {
        return !((*this) == other);
    }
private:
    GeneratorView<T> mView;
    bool mEnd = false; //< Mark is end() iterator
friend class Generator<T>;
};

/**
 * @brief The awaiter used to executor the generator, and return the iterator
 * 
 * @tparam T 
 */
template <typename T>
class GeneratorBeginAwaiter final : public GeneratorAwaiter<T> {
public:
    using Base = GeneratorAwaiter<T>;
    using Base::Base;

    auto await_ready() const noexcept { return false; }

    auto await_suspend(CoroHandle caller) -> bool {
        this->mView.setExecutor(caller.executor());
        if (Base::await_ready()) {
            return false;
        }
        Base::await_suspend(caller);
        return true;
    }
    
    auto await_resume() -> GeneratorIterator<T> {
        Base::await_resume();
        return {this->mView};
    }
};

} // namespace detail

/**
 * @brief The Generator class, used to produce value by yield
 * 
 * @tparam T 
 */
template <typename T>
class Generator {
public:
    using iterator = detail::GeneratorIterator<T>;
    using promise_type = detail::GeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;

    static_assert(!std::is_same_v<T, void>, "Generator can't yield void");

    /**
     * @brief Construct a new empty Generator object
     * 
     */
    Generator() = default;

    /**
     * @brief Construct a new empty Generator object
     * 
     */
    Generator(std::nullptr_t) { }

    /**
     * @brief Construct a new Generator object, disable copy
     * 
     */
    Generator(const Generator &) = delete;

    /**
     * @brief Construct a new Generator object, by move
     * 
     * @param other 
     */
    Generator(Generator &&other) : mHandle(std::exchange(other.mHandle, nullptr)) { }

    /**
     * @brief Destroy the Generator object, destroy the coroutine
     * 
     */
    ~Generator() { clear(); }

    /**
     * @brief Clear the the coroutine in the generator
     * 
     */
    auto clear() -> void {
        if (mHandle) {
            mHandle.destroy();
            mHandle = nullptr;
        }
    }

    /**
     * @brief Get the bgein iterator
     * 
     * @return iterator
     */
    [[nodiscard("Don't forget to use co_await ")]]
    auto begin() -> detail::GeneratorBeginAwaiter<T> {
        return {mHandle};
    }

    /**
     * @brief Get the end iterator
     * 
     * @return iterator 
     */
    auto end() -> iterator {
        return {mHandle, true};
    }

    /**
     * @brief Collect the generated value to a container
     * 
     * @tparam Container 
     * @return Task<Container> 
     */
    template <typename Container = std::vector<T> >
    auto collect() -> Task<Container> {
        Container ret;
        ilias_foreach(T &var, *this) {
            ret.emplace_back(std::move(var));
        }
        co_return ret;
    }

    /**
     * @brief Assign from another generator
     * 
     * @param other 
     * @return Generator& 
     */
    auto operator =(Generator &&other) -> Generator & {
        if (&other != this) {
            return *this;
        }
        clear();
        mHandle = std::exchange(other.mHandle, nullptr);
        return *this;
    }

    /**
     * @brief Assign to the null
     * 
     * @return Generator& 
     */
    auto operator =(std::nullptr_t) -> Generator & {
        clear();
        return *this;
    }

    /**
     * @brief Check the generator is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    Generator(handle_type handle) : mHandle(handle) { }

    handle_type mHandle;
friend class detail::GeneratorPromise<T>;
};

ILIAS_NS_END