#pragma once

#include <ilias/defines.hpp>

#if __cpp_lib_expected < 202202L
    #include <zeus/expected.hpp>
    #define ILIAS_EXPECTED_NAMESPACE ::zeus
#else
    #include <expected>
    #define ILIAS_EXPECTED_NAMESPACE ::std
#endif // ILIAS_EXPECTED_NAMESPACE

// clang 17 actually supports C++20 CTAD but doesn't define __cpp_deduction_guides correctly
#if (!defined(__clang__) && __cpp_deduction_guides < 201907L) || (defined(__clang__) && __clang_major__ < 17)
#error "This library need C++20 CTAD for aggregates and aliases"
#endif // __cpp_deduction_guides

ILIAS_NS_BEGIN

namespace exp = ILIAS_EXPECTED_NAMESPACE;

template <typename T, typename E>
using Result = exp::expected<T, E>;

template <typename E>
using Err = exp::unexpected<E>;

template <typename E>
using BadResultAccess = exp::bad_expected_access<E>;

// For compatibility
template <typename T>
using Unexpected = exp::unexpected<T>;

template <typename E>
using BadExpectedAccess = exp::bad_expected_access<E>;

ILIAS_NS_END