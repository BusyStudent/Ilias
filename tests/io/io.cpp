#include <ilias/fs/console.hpp>
#include <ilias/io.hpp>
#include "testing.hpp"

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

CORO_TEST(Io, Read) {
    char buffer[13];
    auto reader = SpanReader("Hello, world!"_bin);
    auto n = co_await reader.read(makeBuffer(buffer));
    EXPECT_EQ(n, 13);
}

CORO_TEST(Io, Write) {
    auto content = std::string();
    auto writer = StringWriter(content);
    auto n = co_await writer.write("Hello, world!"_bin);

    EXPECT_EQ(n, 13);
    EXPECT_EQ(content, "Hello, world!");
}

CORO_TEST(Io, BufRead) {
    {
        auto reader = SpanReader("Hello, First!\nHello, Next!\n"_bin);
        auto bufReader = BufReader(reader);

        EXPECT_EQ(co_await bufReader.getline(), "Hello, First!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Next!");
        EXPECT_EQ(co_await bufReader.getline(), Err(IoError::UnexpectedEOF));
    }

    {
        auto reader = SpanReader("Hello, First!\nHello, Next!\nHello, Final!"_bin);
        auto bufReader = BufReader(reader);

        EXPECT_EQ(co_await bufReader.getline(), "Hello, First!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Next!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Final!");
        EXPECT_EQ(co_await bufReader.getline(), Err(IoError::UnexpectedEOF));
    }
}

CORO_TEST(Io, BufWrite) {
    auto content = std::string();
    auto writer = StringWriter(content);
    auto bufWriter = BufWriter(writer);

    std::ignore = co_await bufWriter.write("Hello, First!\n"_bin);
    std::ignore = co_await bufWriter.write("Hello, Next!\n"_bin);

    EXPECT_TRUE(content.empty());
    EXPECT_TRUE(co_await bufWriter.flush());
    EXPECT_EQ(content, "Hello, First!\nHello, Next!\n");
}

auto main(int argc, char **argv) -> int {
    runtime::EventLoop loop;
    loop.install();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}