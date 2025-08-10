#pragma once

#include <ilias/defines.hpp>

#if defined(ILIAS_CPP20)
    #include <zeus/expected.hpp>
    #define ILIAS_EXPECTED_NAMESPACE ::zeus
#elif __cpp_lib_expected >= 202202L
    #include <expected>
    #define ILIAS_EXPECTED_NAMESPACE ::std
#else
    #error "This library requires C++23, if you want to use C++20, please add cpp20 in packages config"
#endif // ILIAS_CPP20

#if __cpp_deduction_guides < 201907L
#error "This library need CTAD for aggregates and aliases"
#endif

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