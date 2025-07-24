#pragma once
#include <ilias/task/task.hpp>
#include <ilias/result.hpp>
#include <gtest/gtest.h>
#include <system_error>

using namespace ILIAS_NAMESPACE;

struct CoroFailed {};

#define CORO_ASSERT_EQ(a, b) \
    EXPECT_EQ(a, b);         \

#define CORO_ASSERT_NE(a, b) \
    EXPECT_NE(a, b);         \

#define CORO_ASSERT_TRUE(a)  \
    EXPECT_TRUE(a);          \

#define CORO_ASSERT_FALSE(a) \
    EXPECT_FALSE(a);         \

#define CORO_ASSERT_THROW(expr, type) \
    EXPECT_THROW(type, expr);         \

#define CORO_TEST(name, test)                                \
    Task<void> _##name##_##test();                           \
    TEST(name, test) {                                       \
        try {                                                \
            _##name##_##test().wait();                       \
        }                                                    \
        catch (BadResultAccess<std::error_code> &e) {        \
            std::cerr << "Io Error " << e.error().message(); \
            FAIL();                                          \
        }                                                    \
        catch (CoroFailed &e) { FAIL(); }                    \
    }                                                        \
    Task<void> _##name##_##test()

#if defined(_WIN32)
    #define CORO_USE_UTF8()                      \
        ::SetConsoleCP(65001);       \
        ::SetConsoleOutputCP(65001); \
        std::setlocale(LC_ALL, ".utf-8");                        
    #include <windows.h>
#else
    #define CORO_USE_UTF8()
#endif // _WIN32