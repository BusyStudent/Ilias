#pragma once

#include <ilias/task/task.hpp>
#include <ilias/io/error.hpp>
#include <ilias/io/ext.hpp>
#include <memory>

ILIAS_NS_BEGIN

/**
 * @brief One endpoint of an in-memory full-duplex byte stream.
 *
 * A DuplexStream is created as a connected pair by make(). Data written to one
 * endpoint can be read from the other endpoint, and vice versa. It behaves like
 * a bidirectional in-memory pipe.
 *
 * @par Concurrency
 * The two endpoints returned by make() may be used from different threads.
 *
 * For a single endpoint, the read side and the write side are independent:
 * one read operation and one write operation may be pending at the same time,
 * for example:
 *
 * @code
 * co_await whenAll(stream.read(buf), stream.write(data));
 * @endcode
 *
 * However, operations on the same side are not reentrant. A single endpoint
 * must not have multiple concurrent reads or multiple concurrent writes unless
 * the caller serializes them externally.
 *
 * In other words, this is allowed:
 *
 * @code
 * co_await whenAll(stream.read(buf), stream.write(data));
 * @endcode
 *
 * But this is not allowed:
 *
 * @code
 * co_await whenAll(stream.read(buf1), stream.read(buf2));
 * co_await whenAll(stream.write(data1), stream.write(data2));
 * @endcode
 */
class ILIAS_API DuplexStream final : public StreamExt<DuplexStream> {
public:
    DuplexStream(DuplexStream &&) noexcept = default;
    DuplexStream() = default;

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t>;

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t>;
    auto shutdown(); // IoTask<void>
    auto flush(); // IoTask<void>

    // Cleanup
    auto close() -> void { d.reset(); }

    auto operator =(const DuplexStream &) -> DuplexStream & = delete;
    auto operator =(DuplexStream &&other) -> DuplexStream & = default;

    /**
     * @brief Create a pair of connected duplex stream with the given size.
     * 
     * @param size The size of the buffer. (can't be 0, it will directly call abort)
     * @return std::pair<DuplexStream, DuplexStream> 
     */
    static auto make(size_t size) -> std::pair<DuplexStream, DuplexStream>;

    // Check the stream is valid
    explicit operator bool() const noexcept { return bool(d); }
private:
    // Implementation
    struct Impl;
    static auto shutdownImpl(Impl *d, bool flip) -> void;
    static auto closeImpl(Impl *d, bool flip) -> void;
    
    struct Deleter {
        bool flip; // Use bool flip, to make aother stream write on read buffer and read on write buffer
        auto operator ()(Impl *d) const -> void { closeImpl(d, flip); }
    };

    DuplexStream(Impl *i, bool flip) : d(i, Deleter {flip}) {}
    std::unique_ptr<Impl, Deleter> d;
};

// Implementation
inline auto DuplexStream::shutdown() {
    struct [[nodiscard]] Awaiter {
        auto await_ready() -> bool { return true; }
        auto await_suspend(std::coroutine_handle<> h) -> void {}
        auto await_resume() -> IoResult<void> {
            shutdownImpl(self.d.get(), self.d.get_deleter().flip);
            return {};
        }

        DuplexStream &self;
    };
    return Awaiter {*this};
}

inline auto DuplexStream::flush() {
    return just(IoResult<void>{}); // No-op
}

ILIAS_NS_END