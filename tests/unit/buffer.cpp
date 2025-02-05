#include <ilias/buffer.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(StringPrintf, sprintfSize) {
    EXPECT_EQ(sprintfSize("Hello, %s", "world"), 12);
    EXPECT_EQ(sprintfSize("%d", 123), 3);
}

TEST(StringPrintf, sprintfTo) {
    std::string buf;
    sprintfTo(buf, "Hello, %s", "world");
    EXPECT_EQ(buf, "Hello, world");
    sprintfTo(buf, " %d", 123);
    EXPECT_EQ(buf, "Hello, world 123");
    sprintfTo(buf, " %d", 456);
    EXPECT_EQ(buf, "Hello, world 123 456");
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}