/**
 * @file functional.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Provide move only function type.
 * @version 0.1
 * @date 2024-08-21
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/ilias.hpp>
#include <functional>

ILIAS_NS_BEGIN

namespace detail {

#if defined(__cpp_lib_move_only_function)
template <typename T>
using MoveOnlyFunction = std::move_only_function<T>;
#else
template <typename T>
using MoveOnlyFunction = std::function<T>;
#endif

} // namespace detail

ILIAS_NS_END