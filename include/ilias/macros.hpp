/**
 * @file macros.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Header of user api macros (it can be include when use module)
 * @version 0.1
 * @date 2026-07-24
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <ilias/detail/config.hpp>

// Utils used by public macros
#define ILIAS_CONCAT_(a, b) a##b
#define ILIAS_CONCAT(a, b) ILIAS_CONCAT_(a, b)
#define ILIAS_STRINGIFY_(x) #x
#define ILIAS_STRINGIFY(x) ILIAS_STRINGIFY_(x)

// Version helper
#define ILIAS_VERSION_AT_LEAST(major, minor, patch)                  \
    (ILIAS_VERSION_MAJOR > major ||                                  \
    (ILIAS_VERSION_MAJOR == major && ILIAS_VERSION_MINOR > minor) || \
    (ILIAS_VERSION_MAJOR == major && ILIAS_VERSION_MINOR == minor && ILIAS_VERSION_PATCH >= patch))
#define ILIAS_VERSION_STRING                                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_MAJOR) "."                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_MINOR) "."                         \
    ILIAS_STRINGIFY(ILIAS_VERSION_PATCH)

// result.hpp
// MARK: Try API
// Impl TRY...
#if defined(__clang__) // Using clang's statement expression to optoimize away the temporary
    #define ILIAS_BASIC_TRY_IMPL(var, tmp, ret, ...)           \
        var = ({                                               \
            auto &&tmp = (__VA_ARGS__);                        \
            if (!tmp) {                                        \
                ret ::ilias::makeErr(std::move(tmp));          \
            }                                                  \
            std::move(*tmp);                                   \
        })
#else
    #define ILIAS_BASIC_TRY_IMPL(var, tmp, ret, ...)           \
        auto &&tmp = (__VA_ARGS__);                            \
        if (!tmp) {                                            \
            ret ::ilias::makeErr(std::move(tmp));              \
        }                                                      \
        static_cast<void>(tmp);                                \
        var = std::move(*tmp)
#endif // __clang__

// Impl TRYV...
#define ILIAS_BASIC_TRYV_IMPL(ret, ...)                        \
    do {                                                       \
        if (auto &&_res = (__VA_ARGS__); !_res) {              \
            ret ::ilias::makeErr(std::move(_res));             \
        }                                                      \
    } while (false)

/**
 * @brief Unwrap an expected/optional value inside a coroutine and bind it to a local variable.
 *
 * @param var The name of the local variable to declare.
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`. The expression may contain `co_await`.
 *
 * @note This macro is only valid inside a coroutine whose return type and promise type.
 *
 * @code
 *   auto example() -> IoTask<int> {
 *       ILIAS_CO_TRY(auto data, co_await fetchData());
 *       ILIAS_CO_TRY(auto value, parse(data));
 *       co_return value + 1;
 *   }
 * @endcode
 */
#define ILIAS_CO_TRY(var, ...) ILIAS_BASIC_TRY_IMPL(var, ILIAS_CONCAT(_tmp_, __LINE__), co_return, __VA_ARGS__)

/**
 * @brief Check an expected/optional result inside a coroutine and discard the success value.
 *
 * It evaluates the given expression and continues execution if the expression succeeds.
 * If the expression contains an error, the enclosing coroutine immediately completes by
 * propagating that error to the caller.
 *
 * Use this macro when the success value is not needed, or when the expression returns
 * a void-like result such as `Result<void, E>`.
 *
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`. The expression may contain `co_await`.
 *
 * @note This macro is only valid inside a coroutine whose return type and promise type.
 *
 * @code
 *   auto example() -> IoTask<void> {
 *       ILIAS_CO_TRYV(co_await connect());
 *       ILIAS_CO_TRYV(co_await sendRequest());
 *       co_return {};
 *   }
 * @endcode
 */
#define ILIAS_CO_TRYV(...) ILIAS_BASIC_TRYV_IMPL(co_return, __VA_ARGS__)

// Sync version
/**
 * @brief Unwrap an expected/optional value inside a coroutine and bind it to a local variable.
 *
 * @param var The name of the local variable to declare.
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`.
 *
 * @note This macro is only valid inside a normal function.
 *
 * @code
 *   auto example() -> IoResult<int> {
 *       ILIAS_TRY(auto data, fetchData());
 *       ILIAS_TRY(auto value, parse(data));
 *       return value + 1;
 *   }
 * @endcode
 */
#define ILIAS_TRY(var, ...)  ILIAS_BASIC_TRY_IMPL(var, ILIAS_CONCAT(_tmp_, __LINE__), return, __VA_ARGS__)

/**
 * @brief Check an expected/optional result inside a coroutine and discard the success value.
 *
 * It evaluates the given expression and continues execution if the expression succeeds.
 * If the expression contains an error, the enclosing coroutine immediately completes by
 * propagating that error to the caller.
 *
 * Use this macro when the success value is not needed, or when the expression returns
 * a void-like result such as `Result<void, E>`.
 *
 * @param ... An expression that evaluates to an expected-like type, such as
 *            `Result<T, E>`.
 *
 * @note This macro is only valid inside a normal function.
 *
 * @code
 *   auto example() -> IoResult<void> {
 *       ILIAS_TRYV(connect());
 *       ILIAS_TRYV(sendRequest());
 *       return {};
 *   }
 * @endcode
 */
#define ILIAS_TRYV(...) ILIAS_BASIC_TRYV_IMPL(return, __VA_ARGS__)

// task/generator.hpp
// MARK: For Await API
// Because for(xxx; xxx; co_await(++it)) compile failed in gcc, so we have to use it instead
#define ILIAS_FOR_AWAIT_FALLBACK(var, generator)                                       \
    if (auto &&_gen_ = (generator); false) {}                                          \
    else if (bool _first_ = true; false) {}                                            \
    else                                                                               \
        for (auto _it_ = co_await _gen_.begin(); ; _first_ = false)                    \
            if (!_first_ ? (co_await (++_it_), 0) : 0; _it_ == _gen_.end()) {          \
                break;                                                                 \
            }                                                                          \
            else                                                                       \
                if (var = *_it_; false) {}                                             \
                else 

// Common version
#define ILIAS_FOR_AWAIT_GENERIC(var, generator)                                           \
    if (auto &&_gen_ = (generator); false) {}                                             \
    else                                                                                  \
        for (auto _it_ = co_await _gen_.begin(); _it_ != _gen_.end(); co_await (++_it_))  \
            if (var = *_it_; false) {}

/**
 * @brief The range for for the Generator<T>
 * 
 * @code {.cpp}
 * ilias_for_await(const auto &val, generator()) {
 *  useVal(val);
 * }
 * @endcode
 * 
 * 
 * This macro allows for easy iteration over a generator object.
 * It uses co_await to asynchronously iterate through the generator.
 * 
 * @param var The variable to hold each value from the generator.
 * @param generator The generator object to iterate over.
 */
#define ILIAS_FOR_AWAIT(var, generator) ILIAS_FOR_AWAIT_FALLBACK(var, generator)

/// @copydoc ILIAS_FOR_AWAIT
#define ilias_for_await(var, generator) ILIAS_FOR_AWAIT(var, generator)

// testing.hpp
// MARK: Testing API
// Utf8 setup for Windows
#if defined(_WIN32)
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

/**
 * @brief Create a main testing function, the function body will be call before executing the test cases
 * 
 * @code 
 *  ILIAS_TEST(MyTest, MyTest) {
 *    co_return;
 *  }
 *  ILIAS_TEST_MAIN_F(ilias::EventLoop) {}
 */
#define ILIAS_TEST_MAIN_F(ctxt)                 \
    static auto _ilias_before_test() -> void;   \
    auto main(int argc, char **argv) -> int {   \
        ILIAS_TEST_SETUP_UTF8();                \
        ::testing::InitGoogleTest(&argc, argv); \
        _ilias_before_test();                   \
        ctxt c;                                 \
        c.install();                            \
        return RUN_ALL_TESTS();                 \
    }                                           \
    static auto _ilias_before_test() -> void

#define ILIAS_TEST_MAIN() ILIAS_TEST_MAIN_F(::ilias::PlatformContext)

// platform.hpp
// MARK: Main helper
/**
 * @brief Declare the coroutine main function.
 * 
 * @param ctxt The context type.
 * @param ... The parameters of the main function.
 */
#define ilias_main4(ctxt, ...)                                                      \
    _ilias_tags();                                                                  \
    static auto _ilias_main(__VA_ARGS__) -> ::ilias::Task<decltype(_ilias_tags())>; \
    auto main(int argc, char ** argv) -> int {                                      \
        ctxt context {};                                                            \
        context.install();                                                          \
        auto makeTask = [&](auto callable) {                                        \
            if constexpr (std::invocable<decltype(callable)>) {                     \
                return callable();                                                  \
            }                                                                       \
            else {                                                                  \
                static_assert(                                                      \
                    std::invocable<decltype(callable), int, char **>,               \
                    "Bad main function signature"                                   \
                );                                                                  \
                return callable(argc, argv);                                        \
            }                                                                       \
        };                                                                          \
        auto invoke = [&](auto callable) {                                          \
            auto task = makeTask(callable);                                         \
            if constexpr (std::is_same_v<decltype(task), ::ilias::Task<void> >) {   \
                task.wait();                                                        \
                return 0;                                                           \
            }                                                                       \
            else {                                                                  \
                return task.wait();                                                 \
            }                                                                       \
        };                                                                          \
        return invoke(_ilias_main);                                                 \
    }                                                                               \
    static auto _ilias_main(__VA_ARGS__) -> ::ilias::Task<decltype(_ilias_tags())>       

/**
 * @brief Declare the coroutine main function.
 * 
 * @param ... The parameters of the main function.
 */
#define ilias_main(...) ilias_main4(::ilias::PlatformContext, __VA_ARGS__)