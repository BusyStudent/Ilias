#pragma once

#include <ilias/defines.hpp>
#include <expected>

ILIAS_NS_BEGIN

template <typename T, typename E>
using Result = std::expected<T, E>;

template <typename E>
using Err = std::unexpected<E>;

// For compatibility
template <typename T>
using Unexpected = std::unexpected<T>;

ILIAS_NS_END