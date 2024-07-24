#pragma once

/**
 * @file task.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Include all coroutine related headers. except channel mutex sync classes...
 * @version 0.1
 * @date 2024-07-18
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "coro/promise.hpp"
#include "coro/loop.hpp"
#include "coro/sleep.hpp"
#include "coro/task.hpp"
#include "coro/cancel_handle.hpp"
#include "coro/join_handle.hpp"
#include "coro/coro_handle.hpp"
#include "coro/when_all.hpp"
#include "coro/when_any.hpp"