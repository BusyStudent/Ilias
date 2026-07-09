#include <ilias/platform.hpp>
#include <ilias/testing.hpp>
#include <ilias/task.hpp>
#include <ilias/io.hpp>
#include <gtest/gtest.h>

// Experimental API
#include <ilias/io/vec.hpp>

using namespace ilias;
using namespace ilias::literals;

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
        auto view = std::string_view {reinterpret_cast<const char*>(buffer.data()), buffer.size()};
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
static_assert(BufReadable<BufReader<SpanReader> >);
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
    std::cout << canceled.message() << std::endl;
}

TEST(Io, StreamBuffer) {
    auto buf = StreamBuffer {100};
    ASSERT_EQ(buf.maxCapacity(), 100);
    ASSERT_EQ(buf.size(), 0);

    // Test prepare
    {
        auto prep = buf.prepare(10);
        ASSERT_EQ(prep.size(), 10);
    }
    ASSERT_EQ(buf.size(), 0);


    {
        auto prep = buf.prepare(10);
        ASSERT_EQ(prep.size(), 10);
        ::memset(prep.data(), 0, prep.size());

        // Try commit
        buf.commit(prep.size());
        ASSERT_EQ(buf.size(), 10);
    }

    {
        // Try oversize prepare
        auto prep = buf.prepare(100); // oversize
        ASSERT_TRUE(prep.empty());
    }

    buf.consume(10);
    ASSERT_EQ(buf.size(), 0);

    {
        auto prep = buf.prepare(100);
        ASSERT_EQ(prep.size(), 100);
    }
}

ILIAS_TEST(Io, Read) {
    char buffer[13];
    auto reader = SpanReader{"Hello, world!"_bin};

    EXPECT_EQ(co_await reader.read(makeBuffer(buffer)), 13);
    EXPECT_EQ(std::string_view(buffer, 13), "Hello, world!");
}

ILIAS_TEST(Io, Write) {
    auto content = std::string{};
    auto writer = StringWriter{content};

    EXPECT_EQ(co_await writer.write("Hello, world!"_bin), 13);
    EXPECT_EQ(content, "Hello, world!");
}

ILIAS_TEST(Io, Copy) {
    auto reader = SpanReader{"Hello, world!"_bin};
    auto content = std::string{};
    auto writer = StringWriter{content};

    EXPECT_EQ(co_await io::copy(writer, reader), 13);
    EXPECT_EQ(content, "Hello, world!");
}

ILIAS_TEST(Io, BufRead) {
    {
        auto reader = SpanReader{"Hello, First!\nHello, Next!\n"_bin};
        auto bufReader = BufReader{reader};

        EXPECT_EQ(co_await bufReader.getline(), "Hello, First!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Next!");
        EXPECT_EQ(co_await bufReader.getline(), Err(toKind(IoError::UnexpectedEOF)));
    }

    {
        auto reader = SpanReader{"Hello, First!\nHello, Next!\nHello, Final!"_bin};
        auto bufReader = BufReader{reader};

        EXPECT_EQ(co_await bufReader.getline(), "Hello, First!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Next!");
        EXPECT_EQ(co_await bufReader.getline(), "Hello, Final!");
        EXPECT_EQ(co_await bufReader.getline(), Err(toKind(IoError::UnexpectedEOF)));
    }
}

ILIAS_TEST(Io, BufWrite) {
    {
        auto content = std::string{};
        auto writer = StringWriter{content};
        auto bufWriter = BufWriter{writer};

        std::ignore = co_await bufWriter.write("Hello, First!\n"_bin);
        std::ignore = co_await bufWriter.write("Hello, Next!\n"_bin);

        EXPECT_TRUE(content.empty());
        EXPECT_TRUE(co_await bufWriter.flush());
        EXPECT_EQ(content, "Hello, First!\nHello, Next!\n");
    }

    // Test Capacity
    {
        auto content = std::string{};
        auto writer = StringWriter{content};
        auto bufWriter = BufWriter{writer, 0}; // 0 Capacity

        std::ignore = co_await bufWriter.write("Hello, First!\n"_bin);
        EXPECT_EQ(content, "Hello, First!\n");
        std::ignore = co_await bufWriter.write("Hello, Next!\n"_bin);
        EXPECT_EQ(content, "Hello, First!\nHello, Next!\n");
    }
}

ILIAS_TEST(Io, Duplex) {
    auto [a, b] = DuplexStream::make(10);
    auto sender = [](DuplexStream &stream) -> Task<void> {
        // Test string
        EXPECT_EQ(co_await stream.writeAll("Hello, world!"_bin), 13);
        EXPECT_EQ(co_await stream.writeString("Hello, world!"), 13);

        // Test int
        EXPECT_TRUE(co_await stream.writeUint8(255));
        EXPECT_TRUE(co_await stream.writeUint16BE(2233));
        EXPECT_TRUE(co_await stream.writeUint16LE(2233));
        EXPECT_TRUE(co_await stream.writeUint32BE(1234567890));
        EXPECT_TRUE(co_await stream.writeUint32LE(1234567890));
        EXPECT_TRUE(co_await stream.writeUint64BE(1234567890123456789));
        EXPECT_TRUE(co_await stream.writeUint64LE(1234567890123456789));
    };
    auto receiver = [](DuplexStream &stream) -> Task<void> {
        char buffer[13];
        EXPECT_EQ(co_await stream.readAll(makeBuffer(buffer)), 13);
        EXPECT_EQ(std::string_view(buffer, 13), "Hello, world!");
        EXPECT_EQ(co_await stream.readAll(makeBuffer(buffer)), 13);
        EXPECT_EQ(std::string_view(buffer, 13), "Hello, world!");

        // Test int
        EXPECT_EQ(co_await stream.readUint8(), 255);
        EXPECT_EQ(co_await stream.readUint16BE(), 2233);
        EXPECT_EQ(co_await stream.readUint16LE(), 2233);
        EXPECT_EQ(co_await stream.readUint32BE(), 1234567890);
        EXPECT_EQ(co_await stream.readUint32LE(), 1234567890);
        EXPECT_EQ(co_await stream.readUint64BE(), 1234567890123456789);
        EXPECT_EQ(co_await stream.readUint64LE(), 1234567890123456789);
    };

    // Single thread
    co_await whenAll(sender(a), receiver(b));
    co_await whenAll(sender(b), receiver(a));
    co_await whenAll(receiver(b), sender(a));
    co_await whenAll(receiver(a), sender(b));

    // Cross thread
    co_await whenAll(Thread{sender(a)}, Thread{receiver(b)});
    co_await whenAll(Thread{sender(b)}, Thread{receiver(a)});
    co_await whenAll(Thread{receiver(b)}, Thread{sender(a)});
    co_await whenAll(Thread{receiver(a)}, Thread{sender(b)});

    // Try cancel
    auto worker = [](DuplexStream &stream) -> Task<void> {
        auto val = co_await stream.readInt8();
        ILIAS_TRAP(); // Should not reach here, it was canceled
    };
    {
        auto handle = spawn(worker(a));
        co_await this_coro::yield();
        handle.stop();
        co_await std::move(handle);
    }
    {
        // Cross thread
        Thread thread{worker(b)};
        co_await this_coro::yield();
        thread.stop();
        co_await std::move(thread);
    }

    // Try broken
    EXPECT_TRUE(co_await a.shutdown());
    std::byte tmp[10];
    EXPECT_EQ(co_await b.write("Hello, world!"_bin), 0);
    EXPECT_EQ(co_await b.read(makeBuffer(tmp)), 0);
}

ILIAS_TEST(Io, Readable) {
    auto readOne = [](ReadableView view) -> Task<void> {
        EXPECT_EQ(co_await view.readUint8(), 'H');
    };
    { // from Readable concept
        auto reader = SpanReader{"Hello, world!"_bin};
        co_await readOne(reader);
    }
    { // from Stream concept
        auto [a, b] = DuplexStream::make(10);
        EXPECT_TRUE(co_await b.writeUint8('H'));
        EXPECT_TRUE(co_await b.writeUint8('H'));
        EXPECT_TRUE(co_await b.writeUint8('H'));

        // Test construct directlyy from Stream concept
        co_await readOne(a);
        
        // From StreamView
        co_await readOne(StreamView {a});

        // From DynStream
        auto dyn = DynStream {std::move(a)};
        co_await readOne(dyn);
    }
    { // from DynReadable
        auto reader = DynReadable {SpanReader{"Hello, world!"_bin}};
        co_await readOne(reader);
    }
}

ILIAS_TEST(Io, Writable) {
    auto writeOne = [](WritableView view) -> Task<void> {
        EXPECT_TRUE(co_await view.writeUint8('H'));
    };
    
    { // from Writable concept
        auto content = std::string {};
        auto writer = StringWriter{content};
        co_await writeOne(writer);
        EXPECT_EQ(content, "H");
    }
    {
        // Test operator ==
        auto content = std::string {};
        auto writer = StringWriter{content};
        auto view = WritableView{writer};
        EXPECT_TRUE(view != nullptr);
        EXPECT_TRUE(view == &writer);
    }
    { // from Stream concept
        auto [a, b] = DuplexStream::make(10);

        // As same as above
        co_await writeOne(b);
        EXPECT_EQ(co_await a.readUint8(), 'H');

        co_await writeOne(StreamView {b});
        EXPECT_EQ(co_await a.readUint8(), 'H');

        auto dyn = DynStream {std::move(b)};
        co_await writeOne(dyn);
        EXPECT_EQ(co_await a.readUint8(), 'H');
    }
    { // The DynWritable
        auto content = std::string {};
        auto writer = DynWritable {StringWriter {content}};
        co_await writeOne(writer);
        EXPECT_EQ(content, "H");
    }
}

ILIAS_TEST(Io, Stream) {
    // Test Stream concept
    auto [a, b] = DuplexStream::make(10);
    auto viewA = StreamView {a};
    auto viewB = StreamView {b};
    EXPECT_TRUE(viewA != nullptr);
    EXPECT_TRUE(viewB != nullptr);
    EXPECT_TRUE(viewA == &a);
    EXPECT_TRUE(viewB == &b);
    EXPECT_TRUE(viewA != viewB);
    EXPECT_TRUE(viewA != DynStream {});
    co_return;
}

ILIAS_TEST(Io, MemStream) {
    using namespace std::literals;

    {
        auto reader = MemReader {"Hello, world!"s};

        // Test Read
        EXPECT_EQ(co_await reader.readUint8(), 'H');
        EXPECT_EQ(co_await reader.readUint8(), 'e');

        // Test Seek
        EXPECT_TRUE(co_await reader.rewind());
        EXPECT_EQ(co_await reader.readTo<std::string>(), "Hello, world!");

        // Seek End
        EXPECT_TRUE(co_await reader.seek(0, SeekOrigin::End));
        EXPECT_EQ(co_await reader.tell(), 13);
        EXPECT_FALSE(co_await reader.readUint8()); // EOF

        // Seek Current 
        EXPECT_TRUE(co_await reader.seek(-1, SeekOrigin::Current));
        EXPECT_EQ(co_await reader.readUint8(), '!');
    }
    {
        auto buffer = std::string {};
        auto writer = MemWriter {std::ref(buffer)};

        EXPECT_TRUE(co_await writer.writeString("Hello"));
        EXPECT_TRUE(co_await writer.flush());
        EXPECT_EQ(buffer, "Hello");

        EXPECT_TRUE(co_await writer.rewind());
        EXPECT_TRUE(co_await writer.writeString("World"));
        EXPECT_EQ(buffer, "World"); // Should be overwritten

        // Seek End
        EXPECT_TRUE(co_await writer.seek(0, SeekOrigin::End));
        EXPECT_TRUE(co_await writer.writeString("Hello"));
        EXPECT_EQ(buffer, "WorldHello");
    }

    {
        // Test copy
        auto reader = MemReader {"Hello, world!"sv};
        auto writer = MemWriter {std::string {} };
        EXPECT_TRUE(co_await io::copy(writer, reader));
        EXPECT_EQ(writer.buffer(), "Hello, world!"sv);
    }
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

    auto buf = Buffer{vec};

    // Mutable Version
    char hello[5] = {'H', 'e', 'l', 'l', 'o'};
    auto mutVec = MutableIoVec{makeBuffer(hello)};
    EXPECT_FALSE(mutVec.empty());
    EXPECT_EQ(mutVec.size(), 5);

    auto buf1 = Buffer{mutVec};
    auto mutBuf = MutableBuffer{mutVec};

    // Sequence
    std::vector<Buffer> buffers;
    buffers.emplace_back("Hello"_bin);
    buffers.emplace_back("World"_bin);
    auto seq = makeIoSequence(buffers);
    auto span = std::span{seq};

    std::vector<MutableIoVec> mutableBuffers;
    std::byte subBuffer[1145];
    mutableBuffers.emplace_back(MutableBuffer{subBuffer});
    auto seq1 = makeIoSequence(mutableBuffers); // Mutable -> Const, OK!
}

#if defined(_WIN32)
ILIAS_TEST(Io, Win32Handle) {
    Win32Handle event {
        ::CreateEventW(nullptr, TRUE, FALSE, nullptr)
    };
    EXPECT_TRUE(event);

    // Start an thread to notify the event after 0ms, 10ms, 20ms, 30ms, ...
    for (auto val : std::views::iota(0, 10)) {
        std::thread thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(val * 10));
            ::SetEvent(event.get());
        });
        EXPECT_TRUE(co_await event);
        thread.join();
        ::ResetEvent(event.get());
    }

    // Test Cancel
    {
        auto handle = spawn(event.wait());
        co_await this_coro::yield();
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
}
#endif // _WIN32

ILIAS_TEST_MAIN() {

}