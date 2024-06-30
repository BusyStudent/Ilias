#include <gtest/gtest.h>

#include "../include/ilias_expected.hpp"

#include <iostream>

using ILIAS_NAMESPACE::Expected;
using ILIAS_NAMESPACE::Unexpected;

ILIAS_NS_BEGIN
#include <type_traits>
inline namespace v1 {
#if __cplusplus > 201103L
using ::std::enable_if_t;
#else
template <bool B, class T = void>
using enable_if_t = typename ::std::enable_if<B, T>::type;
#endif
#if __cplusplus >= 201703L
using ::std::is_void_v;
#else
template <class T>
bool constexpr is_void_v = ::std::is_void<T>::value;
#endif
#if __cpp_lib_remove_cvref >= 201711L
using ::std::remove_cvref_t;
#else
template <class T>
using remove_cvref_t = typename ::std::remove_cv<typename ::std::remove_reference<T>::type>::type;
#endif
} // namespace v1
ILIAS_NS_END

class TestError {
public:
    explicit TestError(const int code, const std::string& message) : mCode(code), mMessage(message) {
        std::cout << "\033[92mCreate\033[0m: " << mMessage << " code: " << mCode << std::endl;
    }
    TestError(const TestError& error) : mCode(error.mCode), mMessage(error.mMessage) {
        std::cout << "\033[92mCopy\033[0m: " << mMessage << " code: " << mCode << std::endl;
    }
    const std::string& message() const { return mMessage; }
    const int          code() const { return mCode; }
    TestError&         operator=(const TestError&) = delete;
    ~TestError() { std::cout << "\033[91mDestroy\033[0m: " << mMessage << " code: " << mCode << std::endl; }
    friend std::ostream& operator<<(std::ostream& os, const TestError& error);

private:
    std::string mMessage;
    int         mCode;
};

std::ostream& operator<<(std::ostream& os, const TestError& error) {
    return os << "message: " << error.mMessage << ".[code: " << error.mCode << "]";
}

template <typename T, typename enable = void>
struct print_helper {
    void operator()(const T& t) { std::cout << "\033[32mvalue:\033[0m " << t << std::endl; }
};

template <typename ValueT, typename ErrorT>
struct print_helper<Expected<ValueT, ErrorT>, ILIAS_NAMESPACE::enable_if_t<!ILIAS_NAMESPACE::is_void_v<ValueT>>> {
    void operator()(const Expected<ValueT, ErrorT>& result) {
        if (result.has_value()) {
            std::cout << "\033[32mvalue:\033[0m " << (*result) << std::endl;
        }
        else {
            std::cout << "\033[31merror:\033[0m " << result.error() << std::endl;
        }
    }
};

template <typename ValueT, typename ErrorT>
struct print_helper<Expected<ValueT, ErrorT>, ILIAS_NAMESPACE::enable_if_t<ILIAS_NAMESPACE::is_void_v<ValueT>>> {
    void operator()(const Expected<ValueT, ErrorT>& result) {
        if (result) {
            std::cout << "\033[32mvalue:\033[0m void" << std::endl;
        }
        else {
            std::cout << "\033[31merror:\033[0m " << result.error() << std::endl;
        }
    }
};

template <typename T>
void print(T&& arg) {
    print_helper<ILIAS_NAMESPACE::remove_cvref_t<T>> {}(std::forward<T>(arg));
}
#define OUT(x)                                   \
    std::cout << ">>> [\033[34m" #x "\033[0m] "; \
    print(x);

TEST(Expected, Basic) {
#if !defined(ILIAS_STD_EXPECTED_HPP)
    // T == E
    Expected<int, int> a(23); // Correct value construction
    EXPECT_EQ(a.value(), 23);

    a = Unexpected<int>(23); // Assign an error value via a helper class
    EXPECT_EQ(a.has_value(), false);
    EXPECT_EQ(a.error(), 23);

    a = Unexpected(43); // rvalue operator= from T or E value, And the template assignment does not change the data type
    EXPECT_EQ(a.has_value(), false);
    // EXPECT_EQ(a.value(), 43);

    a = Expected<int, int>(23); // rvalue operator= from Expected, this is a correct value.
    EXPECT_EQ(a.has_value(), true);
    EXPECT_EQ(a.value(), 23);

    int a_value = 43;
    a = a_value; // lvalue operator= from T or E value, And the template assignment does not change the data type
    EXPECT_EQ(a.value(), 43);

    Expected<void, int> b; // void value template
    EXPECT_EQ(b.has_value(), true);

    b = Unexpected(54); // rvalue operator= from E value.
    EXPECT_EQ(b.has_value(), false);
    EXPECT_EQ(b.error(), 54);

    int b_value = 55; // lvalue operator= from E value
    b           = Unexpected(b_value);
    EXPECT_EQ(b.error(), 55);

    Expected<int, TestError> c = 43; // class type in T or E.
    EXPECT_EQ(c.has_value(), true);
    EXPECT_EQ(c.value(), 43);

    c = Unexpected(TestError(43, "error note"));
    EXPECT_EQ(c.has_value(), false);
    EXPECT_EQ(c.error().code(), 43);
    EXPECT_STREQ(c.error().message().c_str(), "error note");
    EXPECT_EQ(c.value_or(42), 42);

    auto c_error = TestError(547, "this is a error");
    c            = Unexpected(c_error);
    EXPECT_EQ(c.has_value(), false);
    EXPECT_EQ(c.error().code(), 547);
    EXPECT_STREQ(c.error().message().c_str(), "this is a error");

    int c_value = 65;
    c           = c_value;
    EXPECT_EQ(c.has_value(), true);
    EXPECT_EQ(c.value(), 65);

    Expected<std::string, int> d = std::string("hello");
    EXPECT_EQ(d.has_value(), true);
    EXPECT_STREQ(d.value().c_str(), "hello");

    d = Unexpected(43);
    EXPECT_EQ(d.has_value(), false);
    EXPECT_EQ(d.error(), 43);

    d = std::string("test for string");
    EXPECT_EQ(d.has_value(), true);
    EXPECT_STREQ(d.value().c_str(), "test for string");

    auto eE = std::move(d);
    EXPECT_STREQ(eE.value().c_str(), "test for string");
    EXPECT_STREQ(d.value().c_str(), "");

    Expected<std::string, TestError> e = std::string("world");
    EXPECT_EQ(e.has_value(), true);
    EXPECT_STREQ(e.value().c_str(), "world");

    e = Unexpected(TestError(43, "error note"));
    EXPECT_EQ(e.has_value(), false);
    EXPECT_EQ(e.error().code(), 43);
    EXPECT_STREQ(e.error().message().c_str(), "error note");

    TestError err = std::move(e.error());
#endif
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}