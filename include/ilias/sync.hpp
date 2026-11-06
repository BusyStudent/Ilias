/**
  ___ _ _             ____              _   _                
 |_ _| (_) __ _ ___  |  _ \ _   _ _ __ | |_(_)_ __ ___   ___ 
  | || | |/ _` / __| | |_) | | | | '_ \| __| | '_ ` _ \ / _ \
  | || | | (_| \__ \ |  _ <| |_| | | | | |_| | | | | | |  __/
 |___|_|_|\__,_|___/ |_| \_\\__,_|_| |_|\__|_|_| |_| |_|\___|

 * @file sync.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Import all headers from the sync module (for sync coroutines)
 * @version 0.1
 * @date 2024-09-23
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/sync/semaphore.hpp>
#include <ilias/sync/oneshot.hpp>
#include <ilias/sync/latch.hpp>
#include <ilias/sync/mutex.hpp>
#include <ilias/sync/event.hpp>
#include <ilias/sync/mpsc.hpp>
#include <ilias/sync/mpmc.hpp>