#pragma once

#include <ilias/task/task.hpp>
#include <ilias/task/when_any.hpp>
#include <chrono>

ILIAS_NS_BEGIN

// Set an timeout for a task, return nullopt on timeout
template <Awaitable T>
inline auto setTimeout(T awaitable, std::chrono::milliseconds ms) -> Task<AwaitableResult<T> > {
    auto [res, timeout] = co_await whenAny(std::move(awaitable), sleep(ms));
    if (timeout) {
        co_return std::nullopt;
    }
    co_return std::move(*res);
}

ILIAS_NS_END