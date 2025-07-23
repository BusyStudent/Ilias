#pragma once
#include <ilias/task/task.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

struct CoroFailed {};

#define CORO_ASSERT_EQ(a, b) \
    EXPECT_EQ(a, b);         \
    throw CoroFailed{};

#define CORO_ASSERT_NE(a, b) \
    EXPECT_NE(a, b);         \
    throw CoroFailed{};

#define CORO_ASSERT_THROW(expr, type) \
    EXPECT_THROW(type, expr);         \
    throw CoroFailed{};

#define CORO_TEST(name, test)                                \
    Task<void> _##name##_##test();                           \
    TEST(name, test) {                                       \
        try {                                                \
            _##name##_##test().wait();                       \
        }                                                    \
        catch (BadExpectedAccess<std::error_code> &e) {      \
            std::cerr << "Io Error " << e.error().message(); \
            FAIL();                                          \
        }                                                    \
        catch (CoroFailed &e) { FAIL(); }                    \
    }                                                        \
    Task<void> _##name##_##test()