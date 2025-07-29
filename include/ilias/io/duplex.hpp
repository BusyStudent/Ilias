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

    DuplexStream(const DuplexStream &) = delete;
    DuplexStream(DuplexStream &&) = default;
    DuplexStream() = default;
    ~DuplexStream() { if(d) close(); }

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t>;

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t>;
    auto shutdown() -> IoTask<void>;
    auto flush() -> IoTask<void>;

    // Cleanup
    auto close() -> void;

    auto operator =(const DuplexStream &) -> DuplexStream & = delete;
    auto operator =(DuplexStream &&other) -> DuplexStream & {
        close();
        d = std::move(other.d);
        mFlip = other.mFlip;
        return *this;
    }

    /**
     * @brief Create a pair of connected duplex stream with the given size.
     * 
     * @param size The size of the buffer. (can't be 0, it will directly call abort)
     * @return std::pair<DuplexStream, DuplexStream> 
     */
    static auto make(size_t size) -> std::pair<DuplexStream, DuplexStream>;

    explicit operator bool() const noexcept { return bool(d); }
private:
    DuplexStream(std::shared_ptr<Impl> i, bool flip) : d(std::move(i)), mFlip(flip) {};
    
    std::shared_ptr<Impl> d;
    bool mFlip = false;
};

ILIAS_NS_END