/**
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
#include <ilias/task.hpp>
#include <gtest/gtest.h>
#include <cstdio>

// Utf8 setup for Windows
#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
    
    #define ILIAS_TEST_SETUP_UTF8()           \
        do {                                  \
            ::SetConsoleCP(65001);            \
            ::SetConsoleOutputCP(65001);      \
            std::setlocale(LC_ALL, ".utf-8"); \
        }                                     \
        while(0)
#else
    #define ILIAS_TEST_SETUP_UTF8() do {} while(0)
#endif // _WIN32

/**
 * @brief The implementation of ILIAS_TEST
 * 
 * @param prefix The unique prefix for the test case
 * @param ... The gtest test case macro
 * 
 */
#define ILIAS_TEST_IMPL(prefix, ...)                                    \
    static auto _ilias_test_##prefix() -> ILIAS_NAMESPACE::Task<void>;  \
    __VA_ARGS__ {                                                       \
        try {                                                           \
            _ilias_test_##prefix().wait();                              \
        }                                                               \
        catch (ILIAS_NAMESPACE::BadResultAccess<std::error_code> &e) {  \
            auto errc = e.error();                                      \
            ::fprintf(                                                  \
                stderr,                                                 \
                "[ilias::Test(%s)] Err %s: (%s)\n",                     \
                #prefix,                                                \
                errc.category().name(),                                 \
                errc.message().c_str()                                  \
            );                                                          \
            FAIL();                                                     \
        }                                                               \
    }                                                                   \
    static auto _ilias_test_##prefix() -> ILIAS_NAMESPACE::Task<void>

/**
 * @brief Create a async test case with gtest
 * 
 * @param name The test suite name
 * @param test The test name
 */
#define ILIAS_TEST(name, test) ILIAS_TEST_IMPL(name##_##test, TEST(name, test))
