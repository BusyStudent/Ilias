#include <ilias/buffer.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

static_assert(BufferSequence<std::vector<Buffer> >);
static_assert(BufferSequence<std::vector<MutableBuffer> >); // Also for non-const
static_assert(MutableBufferSequence<std::vector<MutableBuffer> >);
static_assert(!MutableBufferSequence<std::vector<Buffer> >); // Mutable can to non-mutable, but not vice versa

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