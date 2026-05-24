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
#define ILIAS_TEST_IMPL(prefix, ...)                                  \
    static auto _ilias_test_##prefix() -> ::ilias::Task<void>;        \
    __VA_ARGS__ {                                                     \
        ILIAS_TRY_EXCEPTION {                                         \
            _ilias_test_##prefix().wait();                            \
        }                                                             \
        ILIAS_CATCH (::ilias::BadResultAccess<std::error_code> &e) {  \
            auto errc = e.error();                                    \
            ::fprintf(                                                \
                stderr,                                               \
                "[ilias::Test(%s)] Err %s: (%s)\n",                   \
                #prefix,                                              \
                errc.category().name(),                               \
                errc.message().c_str()                                \
            );                                                        \
            FAIL();                                                   \
        }                                                             \
    }                                                                 \
    static auto _ilias_test_##prefix() -> ::ilias::Task<void>

/**
 * @brief The implementation of ILIAS_RTEST
 * 
 */
#define ILIAS_RTEST_IMPL(prefix, ...)                                 \
    static auto _ilias_rtest_##prefix() -> ::ilias::IoTask<void>;     \
    __VA_ARGS__ {                                                     \
        std::error_code ec {};                                        \
        ILIAS_TRY_EXCEPTION {                                         \
            auto res = _ilias_rtest_##prefix().wait();                \
            ec = res.error_or(std::error_code {});                    \
        }                                                             \
        ILIAS_CATCH (::ilias::BadResultAccess<std::error_code> &e) {  \
            ec = e.error();                                           \
        }                                                             \
        if (ec) {                                                     \
            std::fprintf(                                             \
                stderr,                                               \
                "[ilias::Test(%s)] Err %s: (%s)\n",                   \
                #prefix,                                              \
                ec.category().name(),                                 \
                ec.message().c_str()                                  \
            );                                                        \
            FAIL();                                                   \
        }                                                             \
    }                                                                 \
    static auto _ilias_rtest_##prefix() -> ::ilias::IoTask<void>

/**
 * @brief Create a async test case with gtest
 * 
 * @param name The test suite name
 * @param test The test name
 */
#define ILIAS_TEST(name, test) ILIAS_TEST_IMPL(name##_##test, TEST(name, test))

/**
 * @brief Create a result based test case with gtest
 * @param name The test suite name
 * @param test The test name
 * 
 * @code
 *  ILIAS_RTEST(MyTest, MyTest) {
 *    if (auto res = co_await doSth(); !res) co_return Err(res.error()); 
 *    co_return {};
 *  }
 * @endcode
 */
#define ILIAS_RTEST(name, test) ILIAS_RTEST_IMPL(name##_##test, TEST(name, test))