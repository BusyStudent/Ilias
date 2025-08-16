// INTERNAL !!!
#pragma once

#include <ilias/defines.hpp>
#include <concepts>

ILIAS_NS_BEGIN

// A Helper class for RAII, make code look cleaner
template <std::invocable Fn>
class ScopeExit {
public:
    ScopeExit(Fn fn) : mFn(std::move(fn)) {}
    ScopeExit(const ScopeExit &) = delete;
    ~ScopeExit() { mFn(); }
private:
    Fn mFn;
};

ILIAS_NS_END