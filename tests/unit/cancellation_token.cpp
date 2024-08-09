#include <ilias/cancellation_token.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(CancellationToken, SimpleUsecase) {
    bool value = false;

    CancellationToken token;
    auto reg = token.register_([&]() {
        value = true;
    });
    token.cancel();

    ASSERT_TRUE(value);
    ASSERT_TRUE(token.isCancelled());
}

TEST(CancellationToken, Unregistered) {
    bool value = false;

    CancellationToken token;
    {
        auto reg = token.register_([&]() {
            value = true;
        });    
    }
    token.cancel();

    ASSERT_FALSE(value);
    ASSERT_TRUE(token.isCancelled());
}

TEST(CancellationToken, InvokeAfterCancel) {
    bool value1 = false;
    bool value2 = false;

    CancellationToken token;
    auto reg = token.register_([&]() {
        value1 = true;
    });
    token.cancel();
    auto reg2 = token.register_([&]() {
        value2 = true;
    });

    ASSERT_TRUE(value1);
    ASSERT_TRUE(value2);
    ASSERT_TRUE(token.isCancelled());
}

TEST(CancellationToken, MultipleRegistrations) {
    bool value1 = false;
    bool value2 = false;

    CancellationToken token;
    auto reg1 = token.register_([&]() {
        value1 = true;
    });
    auto reg2 = token.register_([&]() {
        value2 = true;
    });
    token.cancel();

    ASSERT_TRUE(value1);
    ASSERT_TRUE(value2);
    ASSERT_TRUE(token.isCancelled());
}

TEST(CancellationToken, MultipleUnregistered) {
    bool value1 = false;
    bool value2 = false;
    bool value3 = false;

    CancellationToken token;
    {
        auto reg1 = token.register_([&]() {
            value1 = true;
        });
    }
    {
        auto reg2 = token.register_([&]() {
            value2 = true;
        });
    }
    auto reg3 = token.register_([&]() {
        value3 = true;
    });
    token.cancel();

    ASSERT_FALSE(value1);
    ASSERT_FALSE(value2);
    ASSERT_TRUE(value3);
    ASSERT_TRUE(token.isCancelled());
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}