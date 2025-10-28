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

#include <ilias/runtime/executor.hpp>
#include <ilias/runtime/token.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/log.hpp>
#include <coroutine>
#include <optional> // std::optional
#include <vector> // std::vector

/**
 * @brief The range for for the Generator<T>
 * @note Because for(xxx; xxx; co_await(++it)) compile failed in gcc, so we have to use it instead
 * 
 * @code {.cpp}
 * ilias_for_await(const auto &val, generator()) {
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
#define ilias_for_await(var, generator)                                                 \
    if (auto &&_gen_ = (generator); false) { }                                          \
    else if (bool _first_ = true; false) { }                                            \
    else                                                                                \
        for (auto _it_ = co_await _gen_.begin(); ; _first_ = false)                     \
            if (!_first_ ? (co_await (++_it_), 0) : 0; _it_ == _gen_.end()) {           \
                break;                                                                  \
            }                                                                           \
            else                                                                        \
                if (var = *_it_; false) { }                                             \
                else 

ILIAS_NS_BEGIN

namespace task {

using runtime::CoroHandle;
using runtime::CoroPromise;
using runtime::CoroContext;
using runtime::CaptureSource;

// Promsie
template <typename T> requires (!std::is_void_v<T>)
class GeneratorPromise : public CoroPromise {
public:
    using handle_type = std::coroutine_handle<GeneratorPromise<T> >;

    GeneratorPromise() = default;

    // Std coroutines
    auto return_void() const noexcept {}

    auto yield_value(T value) -> runtime::SwitchCoroutine {
        mValue.emplace(std::move(value));
        return { std::exchange(this->mPrevAwaiting, std::noop_coroutine()) }; // Return the coroutine by co_await us
    }

    auto get_return_object(CaptureSource where = {}) noexcept -> Generator<T> {
        this->mCreation = where;
        return { handle() };
    }

    auto handle() noexcept -> handle_type {
        return handle_type::from_promise(*this);
    }
private:
    std::optional<T> mValue;
template <typename U>
friend class GeneratorHandle;
};

// Handle...
template <typename T>
class GeneratorHandle : public CoroHandle {
public:
    using handle_type = std::coroutine_handle<GeneratorPromise<T> >;
    using promise_type = GeneratorPromise<T>;

    GeneratorHandle(handle_type handle) : CoroHandle(handle) { }
    GeneratorHandle() = default;

    auto value() const noexcept -> std::optional<T> & {
        return promise<promise_type>().mValue;
    }
};

/**
 * @brief The awaiter used to execute the generator
 * 
 * @tparam T 
 */
template <typename T>
class [[nodiscard]] GeneratorAwaiter {
public:
    GeneratorAwaiter(GeneratorHandle<T> gen) : mGen(gen) { }

    auto await_ready() const noexcept -> bool {
        mGen.value() = std::nullopt; // Clear the previous value
        mGen.resume();
        return mGen.done() || bool(mGen.value()); // Done? or value yielded
    }

    auto await_suspend(CoroHandle caller) const noexcept -> void {
        mGen.setPrevAwaiting(caller);
    }

    auto await_resume() const -> void { 
        mGen.template promise<CoroPromise>().rethrowIfNeeded();
    }
protected:
    GeneratorHandle<T> mGen; // The handle of the generator, doesn't take the ownership
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
    GeneratorIterator(GeneratorHandle<T> gen) : mGen(gen) { }

    auto operator *() const -> T & {
        return *mGen.value();
    }

    auto operator ->() const -> T * {
        return &*mGen.value();
    }

    // Check end?
    auto operator ==(std::default_sentinel_t) const noexcept -> bool {
        return mGen.done();
    }

    auto operator !=(std::default_sentinel_t) const noexcept -> bool {
        return !mGen.done();
    }

    /**
     * @brief Try to move to the next
     * 
     * @return GeneratorAwaiter<T> 
     */
    auto operator ++() -> GeneratorAwaiter<T> {
        return mGen;
    }
private:
    GeneratorHandle<T> mGen;
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
    
    auto await_resume() -> GeneratorIterator<T> {
        Base::await_resume();
        return {this->mGen};
    }

    // Set the context of the coroutine, call by the await_transform
    auto setContext(runtime::CoroContext &ctxt) -> void {
        this->mGen.setContext(ctxt);
    }
};

} // namespace detail

/**
 * @brief The Generator class, used to produce value by yield
 * 
 * @tparam T The type of the value to be yielded (can't be void)
 */
template <typename T>
class Generator {
public:
    using iterator = task::GeneratorIterator<T>;
    using promise_type = task::GeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;

    static_assert(!std::is_void_v<T>, "Generator can't be used with void type");

    Generator() = default;
    Generator(std::nullptr_t) { }
    Generator(const Generator &) = delete;
    Generator(Generator &&other) : mHandle(std::exchange(other.mHandle, nullptr)) { }
    ~Generator() { clear(); }

    auto clear() -> void {
        if (mHandle) {
            mHandle.destroy();
            mHandle = nullptr;
        }
    }

    [[nodiscard("Don't forget to use co_await ")]]
    auto begin() -> task::GeneratorBeginAwaiter<T> {
        return {mHandle};
    }

    auto end() -> std::default_sentinel_t {
        return std::default_sentinel;
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
        ilias_for_await(T &var, *this) {
            ret.emplace_back(std::move(var));
        }
        co_return ret;
    }

    auto operator =(Generator &&other) -> Generator & {
        if (&other != this) {
            return *this;
        }
        clear();
        mHandle = std::exchange(other.mHandle, nullptr);
        return *this;
    }

    auto operator =(std::nullptr_t) -> Generator & {
        clear();
        return *this;
    }

    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    Generator(handle_type handle) : mHandle(handle) { }

    handle_type mHandle;
friend class task::GeneratorPromise<T>;
};

ILIAS_NS_END