/**
  ___ _ _             ____              _   _                
 |_ _| (_) __ _ ___  |  _ \ _   _ _ __ | |_(_)_ __ ___   ___ 
  | || | |/ _` / __| | |_) | | | | '_ \| __| | '_ ` _ \ / _ \
  | || | | (_| \__ \ |  _ <| |_| | | | | |_| | | | | | |  __/
 |___|_|_|\__,_|___/ |_| \_\\__,_|_| |_|\__|_|_| |_| |_|\___|

 * @file testing.hpp
 * @author your name (you@domain.com)
 * @brief The testing utilities for async & gtest
 * @version 0.1
 * @date 2025-09-22
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/result.hpp>
#include <ilias/macros.hpp> // ILIAS_TEST*
#include <ilias/task.hpp>
#include <gtest/gtest.h>
#include <cstdio>