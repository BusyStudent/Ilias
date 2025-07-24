#pragma once

#include <ilias/defines.hpp>
#include <expected>

#if __cpp_lib_expected < 202202L
#error "This library requires C++23"
#endif

#if __cpp_deduction_guides < 201907L
#error "This library need CTAD for aggregates and aliases"
#endif

ILIAS_NS_BEGIN

template <typename T, typename E>
using Result = std::expected<T, E>;

template <typename E>
using Err = std::unexpected<E>;

template <typename E>
using BadResultAccess = std::bad_expected_access<E>;

// For compatibility
template <typename T>
using Unexpected = std::unexpected<T>;

template <typename E>
using BadExpectedAccess = std::bad_expected_access<E>;

ILIAS_NS_END