#pragma once

#include <ilias/task/task.hpp>
#include <ilias/io/error.hpp>

ILIAS_NS_BEGIN

namespace signal {
    
/**
 * @brief Waiting for SIGINT signal
 * 
 * @return Ok on SIGINT, Err on system error
 */
extern auto ILIAS_API ctrlC() -> IoTask<void>;

#if defined(__linux__)
/**
 * @brief Waiting for the specified signal
 * 
 * @param sig The signal number like SIGINT, SIGQUIT, etc.
 * @return IoTask<void> 
 */
extern auto ILIAS_API signal(int sig) -> IoTask<void>;
#endif

} // namespace signal

ILIAS_NS_END