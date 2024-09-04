#include <ilias/buffer.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(BufferTest, Expendable) {
    std::string output;
    MemWriter writer(output);

    ASSERT_TRUE(writer.puts("Hello, world!"));
    ASSERT_TRUE(writer.puts("Hello, world!"));

    EXPECT_EQ(writer.bytesWritten(), 26);
}

TEST(BufferTest, NonExpendable) {
    char buf[10];
    MemWriter writer(buf);

    ASSERT_FALSE(writer.puts("Hello, world!")); //< buffer too small
    ASSERT_TRUE(writer.puts("AA"));
    ASSERT_TRUE(writer.puts("BB"));
    ASSERT_TRUE(writer.puts("CC"));
    ASSERT_TRUE(writer.puts("DD"));
    ASSERT_TRUE(writer.puts("EE")); //< buffer full
    ASSERT_FALSE(writer.puts("FF")); //< buffer full

    EXPECT_EQ(writer.bytesWritten(), 10);
    EXPECT_EQ(std::string(buf, 10), "AABBCCDDEE");
}

TEST(BufferTest, Printf) {
    std::vector<std::byte> buf;
    MemWriter writer(buf);

    writer.printf("GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n", "foo", "bar");

#if defined(__cpp_lib_format)
    writer.print("GET /{} HTTP/1.1\r\nHost: {}\r\n\r\n", "foo", "bar");
#endif

}

TEST(BufferTest, Read) {
    std::string input = "Hello, world!";
    MemReader reader(input);

    char buf[2];
    ASSERT_TRUE(reader.read(makeBuffer(buf)));
    EXPECT_EQ(std::string(buf, 2), "He");
    ASSERT_TRUE(reader.read(makeBuffer(buf)));
    EXPECT_EQ(std::string(buf, 2), "ll");
    ASSERT_TRUE(reader.read(makeBuffer(buf)));
    EXPECT_EQ(std::string(buf, 2), "o,");
    ASSERT_TRUE(reader.read(makeBuffer(buf)));
    EXPECT_EQ(std::string(buf, 2), " w");
    ASSERT_TRUE(reader.read(makeBuffer(buf)));
    EXPECT_EQ(std::string(buf, 2), "or");
    ASSERT_TRUE(reader.read(makeBuffer(buf)));
    EXPECT_EQ(std::string(buf, 2), "ld");
    ASSERT_FALSE(reader.read(makeBuffer(buf))); //< only one byte left

    EXPECT_EQ(reader.bytesReaded(), 12);
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}