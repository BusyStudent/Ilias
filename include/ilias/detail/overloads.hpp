#pragma once

#include <ilias/defines.hpp>

ILIAS_NS_BEGIN

template <typename ...Ts>
struct Overloads : Ts... { using Ts::operator()...; };

ILIAS_NS_END