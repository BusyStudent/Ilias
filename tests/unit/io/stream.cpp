#include <ilias/io/stream.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

class StreamBufferTest : public testing::Test {
protected:
    StreamBuffer buffer;
};

TEST_F(StreamBufferTest, BasicWriteRead) {
    std::string_view testData = "Test Data";
    size_t len = testData.size();
    
    // Test write
    auto writeSpan = buffer.prepare(len);
    ASSERT_EQ(writeSpan.size(), len);
    ::memcpy(writeSpan.data(), testData.data(), len);
    buffer.commit(len);
    
    // Test reading
    auto readSpan = buffer.data();
    ASSERT_EQ(readSpan.size(), len);
    ASSERT_EQ(::memcmp(readSpan.data(), testData.data(), len), 0);
}

TEST_F(StreamBufferTest, Expansion) {
    // Test expansion
    std::vector<char> largeData(1024, 'A');
    
    for (int i = 0; i < 10; i++) {
        auto span = buffer.prepare(1024);
        ASSERT_EQ(span.size(), 1024);
        ::memcpy(span.data(), largeData.data(), 1024);
        buffer.commit(1024);
    }
    
    ASSERT_EQ(buffer.size(), 10240);
}

TEST_F(StreamBufferTest, MoveOperations) {
    // Test move constructor and move assignment
    const char *testData = "Test Data";
    auto span = buffer.prepare(9);
    ::memcpy(span.data(), testData, 9);
    buffer.commit(9);
    
    StreamBuffer buffer2(std::move(buffer));
    ASSERT_EQ(buffer.size(), 0);
    ASSERT_EQ(buffer2.size(), 9);
    
    StreamBuffer buffer3;
    buffer3 = std::move(buffer2);
    ASSERT_EQ(buffer2.size(), 0);
    ASSERT_EQ(buffer3.size(), 9);
}

TEST_F(StreamBufferTest, ConsumeBehavior) {
    std::string_view testData = "ABCDEFGHIJK";
    size_t len = testData.size();
    
    auto span = buffer.prepare(len);
    memcpy(span.data(), testData.data(), len);
    buffer.commit(len);
    
    // Partially consume
    buffer.consume(5);
    ASSERT_EQ(buffer.size(), len - 5);
    auto readSpan = buffer.data();
    ASSERT_EQ(::memcmp(readSpan.data(), testData.data() + 5, len - 5), 0);
}

TEST_F(StreamBufferTest, MaxCapacity) {
    // Test max capacity
    StreamBuffer limitedBuffer(100);
    
    auto span = limitedBuffer.prepare(150);
    ASSERT_EQ(span.size(), 0); // In limited buffer, should not be able to prepare more than max capacity
    
    span = limitedBuffer.prepare(50);
    ASSERT_EQ(span.size(), 50); // Should be able to prepare up to max capacity
}

auto main(int argc, char **argv) -> int {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}