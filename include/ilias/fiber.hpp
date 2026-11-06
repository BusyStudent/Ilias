/**
  ___ _ _             ____              _   _                
 |_ _| (_) __ _ ___  |  _ \ _   _ _ __ | |_(_)_ __ ___   ___ 
  | || | |/ _` / __| | |_) | | | | '_ \| __| | '_ ` _ \ / _ \
  | || | | (_| \__ \ |  _ <| |_| | | | | |_| | | | | | |  __/
 |___|_|_|\__,_|___/ |_| \_\\__,_|_| |_|\__|_|_| |_| |_|\___|

 * @file fiber.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The stackful coroutine, useful for some asyncify blocking code.
 * @version 0.1
 * @date 2026-11-06
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#pragma once

#include <ilias/defines.hpp>

#if defined(ILIAS_USE_FIBER)
    #include <ilias/fiber/fiber.hpp>
#endif // ILIAS_USE_FIBER