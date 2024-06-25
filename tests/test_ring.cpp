#include <gtest/gtest.h>

#define ILIAS_RING_DEBUG

#include "../include/ilias_async.hpp"
#include "../include/ilias_loop.hpp"
#include "../include/ilias_ssl.hpp"

using namespace ILIAS_NAMESPACE;

TEST(RingTest, Test1) {

    RingBuffer<20, char> ring;
    EXPECT_EQ(ring.push("123", 3), 3);
    EXPECT_EQ(ring.push("456", 3), 3);
    EXPECT_EQ(ring.push("789", 3), 3);
    EXPECT_EQ(ring.push("123", 3), 3);
    EXPECT_EQ(ring.push("456", 3), 3);
    EXPECT_EQ(ring.push("789", 3), 3);
    EXPECT_EQ(ring.push("123", 3), 2);

    char buf[21] = {0};
    EXPECT_EQ(ring.pop(buf, 15), 15);
    EXPECT_STREQ((char*)buf, "123456789123456");
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ring.pop(buf, 15), 5);
    EXPECT_STREQ((char*)buf, "78912");
    EXPECT_EQ(ring.pop(buf, 15), 0);

    memset(buf, '1', sizeof(buf));
    EXPECT_EQ(ring.push(buf, 15), 15);
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ring.pop(buf, 14), 14);

    memset(buf, '2', sizeof(buf));
    EXPECT_EQ(ring.push(buf, 19), 19);
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ring.pop(buf, 20), 20);
    EXPECT_STREQ((char*)buf, "12222222222222222222");

    memset(buf, '1', sizeof(buf));
    EXPECT_EQ(ring.push(buf, 15), 15);
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ring.pop(buf, 14), 14);

    memset(buf, '2', sizeof(buf));
    EXPECT_EQ(ring.push(buf, 13), 13);
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ring.pop(buf, 20), 14);
}

TEST(RingTest, Test2) {
    RingBuffer<10, char> ring;
    EXPECT_EQ(ring.push("123", 3), 3);
    EXPECT_EQ(ring.push("456", 3), 3);
    EXPECT_EQ(ring.push("789", 3), 3);
    
    char tmpbuf[3];
    memset(tmpbuf, 0, sizeof(tmpbuf));
    EXPECT_EQ(ring.pop(tmpbuf, 3), 3);

    // Now is still continuous
    EXPECT_TRUE(ring.continuous());

    // Should not continuous
    EXPECT_EQ(ring.push("1234", 4), 4);
    EXPECT_FALSE(ring.continuous());
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}