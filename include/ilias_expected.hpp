#pragma once
#include <expected>

#include "ilias.hpp"

ILIAS_NS_BEGIN

template <typename T, typename E>
using Expected = std::expected<T, E>;
template <typename E>
using Unexpected = std::unexpected<E>;

ILIAS_NS_END