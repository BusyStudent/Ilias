#pragma once

#include <ilias/io/traits.hpp>
#include <ilias/io/ext.hpp>
#include <ilias/buffer.hpp>
#include <functional> // std::reference_wrapper
#include <variant> // std::monostate
#include <cstring> // memcpy

ILIAS_NS_BEGIN

/**
 * @brief Wrapping the memory buffer as a stream 
 * 
 * @tparam T std::string, std::vector<char>, or Buffer etc.
 */
template <typename T> requires(MemReadable<T> || MemWritable<T>)
class MemStream final : public StreamExt<MemStream<T> >,
                        public SeekableExt<MemStream<T> > {
public:
    constexpr MemStream(T buffer) requires(!std::is_reference_v<T>) : mBuffer(std::move(buffer)) {}
    constexpr MemStream(T &buffer) requires(std::is_reference_v<T>) : mBuffer(buffer) {}
    constexpr MemStream(MemStream &&) = default;

    // Readable
    auto read(MutableBuffer buffer) requires(MemReadable<T>) {
        struct Awaiter {
            auto await_ready() -> bool { return true; }
            auto await_suspend(std::coroutine_handle<> handle) -> void {}
            auto await_resume() -> IoResult<size_t> { return stream.doRead(buffer); }

            MemStream &stream;
            MutableBuffer buffer;
        };
        return Awaiter {
            .stream = *this,
            .buffer = buffer
        };
    }

    // Writable
    auto write(Buffer buffer) requires(MemWritable<T>) {
        struct Awaiter {
            auto await_ready() -> bool { return true; }
            auto await_suspend(std::coroutine_handle<> handle) -> void {}
            auto await_resume() -> IoResult<size_t> { return stream.doWrite(buffer); }

            MemStream &stream;
            Buffer buffer;
        };
        return Awaiter {
            .stream = *this,
            .buffer = buffer
        };
    }

    auto shutdown() requires(MemWritable<T>) { 
        IoResult<void> value{};
        return just(value); // No-op
    }

    auto flush() requires(MemWritable<T>) { 
        IoResult<void> value{};
        return just(value); // No-op
    }

    // Seekable
    auto seek(int64_t offset, SeekOrigin origin) {
        struct Awaiter {
            auto await_ready() -> bool { return true; }
            auto await_suspend(std::coroutine_handle<> handle) -> void {}
            auto await_resume() -> IoResult<uint64_t> { return stream.doSeek(offset, origin); }

            MemStream &stream;
            int64_t    offset;
            SeekOrigin origin;
        };
        return Awaiter {
            .stream = *this,
            .offset = offset,
            .origin = origin
        };
    }

    // Get the memory buffer reference
    auto buffer() -> T & { return mBuffer; }
    auto buffer() const -> const T & { return mBuffer; }

    // Check if we can append on this stream
    static constexpr auto expendable() -> bool {
        using Element = decltype(*std::begin(std::declval<T>())); // Check is bytes container
        return MemWritable<T> && MemExpendable<T> && sizeof(Element) == 1;
    }
private:
    auto doRead(MutableBuffer buffer) -> size_t {
        auto self = makeBuffer(mBuffer).subspan(mPos);
        auto bytes = std::min(buffer.size(), self.size());
        if (bytes > 0) {
            ::memcpy(buffer.data(), self.data(), bytes);
        }
        mPos += bytes;
        return bytes;
    }

    auto doWrite(Buffer buffer) -> size_t {
        if (buffer.empty()) {
            return 0;
        }
        while (true) {
            auto self = makeBuffer(mBuffer);
            if (mPos >= self.size()) { // Append
                if constexpr (expendable()) {
                    mBuffer.resize(mPos + buffer.size());
                    continue;
                }
                else {
                    return 0; // No space left
                }
            }
            // Move to its position
            self = self.subspan(mPos);
            auto bytes = std::min(buffer.size(), self.size());
            if (bytes > 0) {
                ::memcpy(self.data(), buffer.data(), bytes);
            }
            mPos += bytes;
            return bytes;
        }
    }

    auto doSeek(int64_t offset, SeekOrigin origin) -> uint64_t {
        auto self = makeBuffer(mBuffer);
        auto size = static_cast<int64_t>(self.size());
        auto pos = static_cast<int64_t>(mPos);
        switch (origin) {
            case SeekOrigin::Begin: pos = offset; break;
            case SeekOrigin::Current: pos += offset; break;
            case SeekOrigin::End: pos = static_cast<int64_t>(size) + offset; break;
        }
        if (pos < 0) {
            pos = 0;
        }
        else if (pos > size) {
            pos = size;
        }
        mPos = pos;
        return mPos;
    }

    T      mBuffer;    // The buffer to read from
    size_t mPos {};    // How many bytes have been read
};

template <typename T>
MemStream(T stream) -> MemStream<T>;

template <typename T>
MemStream(std::reference_wrapper<T> stream) -> MemStream<T &>;

template <typename T>
using MemReader = MemStream<T>;

template <typename T>
using MemWriter = MemStream<T>;

ILIAS_NS_END