#pragma once

#include <ilias/io/context.hpp>
#include <ilias/task/task.hpp>

ILIAS_NS_BEGIN

namespace signal {
    
/**
 * @brief Waiting for SIGINT signal
 * 
 * @return Ok on SIGINT, Err on system error
 */
extern auto ILIAS_API ctrlC() -> IoTask<void>;

} // namespace signal

ILIAS_NS_END