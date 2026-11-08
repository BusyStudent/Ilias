/**
  ___ _ _             ____              _   _                
 |_ _| (_) __ _ ___  |  _ \ _   _ _ __ | |_(_)_ __ ___   ___ 
  | || | |/ _` / __| | |_) | | | | '_ \| __| | '_ ` _ \ / _ \
  | || | | (_| \__ \ |  _ <| |_| | | | | |_| | | | | | |  __/
 |___|_|_|\__,_|___/ |_| \_\\__,_|_| |_|\__|_|_| |_| |_|\___|

 * @file signal.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Waiting for signals
 * @version 0.1
 * @date 2026-11-06
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/task/task.hpp>
#include <ilias/io/error.hpp>
#include <csignal> // SIG*

ILIAS_EXPORT
ILIAS_NS_BEGIN

namespace signal {
    
/**
 * @brief Waiting for SIGINT signal
 * 
 * @return Ok on SIGINT, Err on system error
 */
ILIAS_API extern auto ctrlC() -> IoTask<void>;

#if defined(__linux__)
/**
 * @brief Waiting for the specified signal
 * 
 * @param sig The signal number like SIGINT, SIGQUIT, etc.
 * @return IoTask<void> 
 */
ILIAS_API extern auto waitFor(int sig) -> IoTask<void>;
#endif // __linux__

#if defined(_WIN32)
/**
 * @brief Waiting for the specified ctrl event
 * 
 * @param ctrlEvent The ctrl event number like CTRL_C_EVENT, CTRL_BREAK_EVENT, etc.
 * @return IoTask<void> 
 */
ILIAS_API extern auto waitCtrl(int ctrlEvent) -> IoTask<void>;
#endif // _WIN32

} // namespace signal

ILIAS_NS_END