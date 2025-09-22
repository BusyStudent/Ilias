#include <ilias/task.hpp>
#include <ilias/io.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>

// Experimental API
#include <ilias/io/vec.hpp>

using namespace ILIAS_NAMESPACE;
using namespace ILIAS_NAMESPACE::literals;

class SpanReader {
public:
    SpanReader(Buffer buffer) : mBuffer(buffer) {};

    auto read(MutableBuffer buffer) -> IoTask<size_t> {
        auto left = std::min(buffer.size(), mBuffer.size());
        ::memcpy(buffer.data(), mBuffer.data(), left);
        mBuffer = mBuffer.subspan(left);
        co_return left;
    }
private:
    Buffer mBuffer;
};

class StringWriter {
public:
    StringWriter(std::string &str) : mStr(str) {};

    auto write(Buffer buffer) -> IoTask<size_t> {
        auto view = std::string_view(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        mStr += view;
        co_return buffer.size();
    }

    auto shutdown() -> IoTask<void> {
        co_return {};
    }

    auto flush() -> IoTask<void> {
        co_return {};
    }
private:
    std::string &mStr;
};

// Static assertions
static_assert(Readable<BufReader<SpanReader> >);
static_assert(!Writable<BufReader<SpanReader> >);

TEST(Io, Error) {
    auto ec = make_error_code(IoError::UnexpectedEOF);
    auto canceled = make_error_code(SystemError::Canceled);

    ASSERT_EQ(ec, IoError::UnexpectedEOF);
    ASSERT_EQ(ec, toKind(IoError::UnexpectedEOF));
    
    ASSERT_EQ(canceled, SystemError::Canceled);
    ASSERT_EQ(canceled, toKind(IoError::Canceled));
    ASSERT_EQ(canceled, std::errc::operation_canceled);

    ASSERT_EQ(make_error_code(IoError::Canceled), std::errc::operation_canceled);
    std::cout << ec.message() << std::endl;
}

ILIAS_TEST(Io, Read) {
    char buffer[13];
    auto reader = SpanReader("Hello, world!"_bin);

    EXPECT_EQ(co_await reader.read(makeBuffer(buffer)), 13);
    EXPECT_EQ(std::string_view(buffer, 13), "Hello, world!");
}

ILIAS_TEST(Io, Write) {
    auto content = std::string();
    auto writer = StringWriter(content);

    EXPECT_EQ(co_await writer.write("Hello, world!"_bin), 13);
    EXPECT_EQ(content, "Hello, world!");
}

ILIAS_TEST(Io, BufRead) {
    {
        auto reader = SpanReader("Hello, First!\nHello, Next!\n"_bin);
        auto bufReader = BufReader(reader);

        EXPECT_EQ(co_await bufReader.getline(), "Hello, First!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Next!");
        EXPECT_EQ(co_await bufReader.getline(), Err(toKind(IoError::UnexpectedEOF)));
    }

    {
        auto reader = SpanReader("Hello, First!\nHello, Next!\nHello, Final!"_bin);
        auto bufReader = BufReader(reader);

        EXPECT_EQ(co_await bufReader.getline(), "Hello, First!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Next!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Final!");
        EXPECT_EQ(co_await bufReader.getline(), Err(toKind(IoError::UnexpectedEOF)));
    }
}

ILIAS_TEST(Io, BufWrite) {
    auto content = std::string();
    auto writer = StringWriter(content);
    auto bufWriter = BufWriter(writer);

    std::ignore = co_await bufWriter.write("Hello, First!\n"_bin);
    std::ignore = co_await bufWriter.write("Hello, Next!\n"_bin);

    EXPECT_TRUE(content.empty());
    EXPECT_TRUE(co_await bufWriter.flush());
    EXPECT_EQ(content, "Hello, First!\nHello, Next!\n");
}

ILIAS_TEST(Io, Duplex) {
    auto [a, b] = DuplexStream::make(10);
    auto sender = [](auto &stream) -> Task<void> {
        EXPECT_EQ(co_await stream.writeAll("Hello, world!"_bin), 13);
    };
    auto receiver = [](auto &stream) -> Task<void> {
        char buffer[13];
        EXPECT_EQ(co_await stream.readAll(makeBuffer(buffer)), 13);
        EXPECT_EQ(std::string_view(buffer, 13), "Hello, world!");
    };

    co_await whenAll(sender(a), receiver(b));
    co_await whenAll(sender(b), receiver(a));
    co_await whenAll(receiver(b), sender(a));
    co_await whenAll(receiver(a), sender(b));

    // Try broken
    a.close();
    std::byte tmp[10];
    EXPECT_EQ(co_await b.write("Hello, world!"_bin), 0);
    EXPECT_EQ(co_await b.read(makeBuffer(tmp)), 0);
}

TEST(Experimental, IoVec) {
    static_assert(std::is_trivially_destructible_v<IoVec>);
    static_assert(std::is_trivially_destructible_v<MutableIoVec>);

    auto vec = IoVec{};
    auto vec2 = IoVec{"Hello"_bin};
    EXPECT_TRUE(vec.empty());
    EXPECT_EQ(vec.data(), nullptr);
    EXPECT_EQ(vec.size(), 0);
    EXPECT_EQ(vec, IoVec{}); 
    EXPECT_NE(vec, vec2);

    auto buf = Buffer(vec);

    // Mutable Version
    char hello[5] = {'H', 'e', 'l', 'l', 'o'};
    auto mutVec = MutableIoVec(makeBuffer(hello));
    EXPECT_FALSE(mutVec.empty());
    EXPECT_EQ(mutVec.size(), 5);

    auto buf1 = Buffer(mutVec);
    auto mutBuf = MutableBuffer(mutVec);

    // Sequence
    std::vector<Buffer> buffers;
    buffers.emplace_back("Hello"_bin);
    buffers.emplace_back("World"_bin);
    auto seq = makeIoSequence(buffers);
    auto span = std::span(seq);

    std::vector<MutableIoVec> mutableBuffers;
    std::byte subBuffer[1145];
    mutableBuffers.emplace_back(MutableBuffer(subBuffer));
    auto seq1 = makeIoSequence(mutableBuffers); // Mutable -> Const, OK!
}

auto main(int argc, char **argv) -> int {
    EventLoop loop;
    loop.install();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}