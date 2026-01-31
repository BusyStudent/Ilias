#pragma once

#include <ilias/task/task.hpp>
#include <ilias/io/method.hpp>
#include <ilias/io/error.hpp>
#include <memory>

ILIAS_NS_BEGIN

/**
 * @brief A Duplex stream is a stream that can be read from and written to at the same time. like a pipe. but in memory.
 * 
 */
class ILIAS_API DuplexStream final : public StreamMethod<DuplexStream> {
public:
    struct Impl;

    DuplexStream(DuplexStream &&) noexcept = default;
    DuplexStream() = default;
    ~DuplexStream() = default;

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t>;

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t>;
    auto shutdown() -> IoTask<void>;
    auto flush() -> IoTask<void>;

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

    explicit operator bool() const noexcept { return bool(d); }
private:
    static auto closeImpl(Impl *d, bool flip) -> void;
    struct Deleter {
        bool flip; // Use bool flip, to make aother stream write on read buffer and read on write buffer
        auto operator ()(Impl *d) const -> void { closeImpl(d, flip); }
    };

    DuplexStream(Impl *i, bool flip) : d(i, Deleter {flip}) {}
    std::unique_ptr<Impl, Deleter> d;
};

ILIAS_NS_END