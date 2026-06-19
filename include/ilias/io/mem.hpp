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
    auto read(MutableBuffer buffer) -> IoTask<size_t> requires(MemReadable<T>) {
        auto self = makeBuffer(mBuffer).subspan(mPos);
        auto bytes = std::min(buffer.size(), self.size());
        if (bytes > 0) {
            ::memcpy(buffer.data(), self.data(), bytes);
        }
        mPos += bytes;
        co_return bytes;
    }

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t> requires(MemWritable<T>) {
        if (buffer.empty()) {
            co_return 0;
        }
        while (true) {
            auto self = makeBuffer(mBuffer);
            if (mPos >= self.size()) { // Append
                if constexpr (expendable()) {
                    mBuffer.resize(mPos + buffer.size());
                    continue;
                }
                else {
                    co_return 0; // No space left
                }
            }
            // Move to its position
            self = self.subspan(mPos);
            auto bytes = std::min(buffer.size(), self.size());
            if (bytes > 0) {
                ::memcpy(self.data(), buffer.data(), bytes);
            }
            mPos += bytes;
            co_return bytes;
        }
    }

    auto shutdown() -> IoTask<void> requires(MemWritable<T>) { 
        co_return {}; // No-op
    }

    auto flush() -> IoTask<void> requires(MemWritable<T>) { 
        co_return {}; // No-op
    }

    // Seekable
    auto seek(int64_t offset, SeekOrigin origin) -> IoTask<uint64_t> {
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
        co_return mPos;
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