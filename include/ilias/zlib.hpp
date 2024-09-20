/**
 * @file zlib.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Wrapper for zlib
 * @version 0.1
 * @date 2024-09-02
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once

#if __has_include(<zlib.h>) && !defined(ILIAS_NO_ZLIB)

#include <ilias/detail/expected.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/buffer.hpp>
#include <ilias/error.hpp>
#include <ilias/log.hpp>
#include <vector>
#include <zlib.h>
#include <span>

ILIAS_NS_BEGIN

namespace zlib {

/**
 * @brief The zlib compression formats
 * 
 */
enum ZFormat : int {
    GzipFormat = 16 + MAX_WBITS,
    DeflateFormat = MAX_WBITS,
};

/**
 * @brief The zlib error codes
 * 
 */
enum ZError : int {
    Ok = Z_OK,
    StreamError = Z_STREAM_ERROR,
    DataError = Z_DATA_ERROR,
    MemError = Z_MEM_ERROR,
    StreamEnd = Z_STREAM_END,
    NeedDict = Z_NEED_DICT,
};

/**
 * @brief Error category for zlib
 * 
 */
class ZCategory : public ErrorCategory {
public:
    auto name() const -> std::string_view override {
        return "zlib";
    }

    auto message(int64_t ev) const -> std::string override {
        switch (ev) {
            case ZError::Ok: return "Z_OK";
            case ZError::StreamError: return "Z_STREAM_ERROR";
            case ZError::DataError: return "Z_DATA_ERROR";
            case ZError::MemError: return "Z_MEM_ERROR";
            case ZError::StreamEnd: return "Z_STREAM_END";
            case ZError::NeedDict: return "Z_NEED_DICT";
            default: return "Unknown";
        }
    }

    /**
     * @brief Get the global instance of ZCategory
     * 
     * @return ZCategory& 
     */
    static auto instance() -> ZCategory & {
        static ZCategory c;
        return c;
    }
};

ILIAS_DECLARE_ERROR(ZError, ZCategory);

/**
 * @brief The zlib decompressor
 * 
 */
class Decompressor {
public:
    /**
     * @brief Construct a new Decompressor object by format
     * 
     * @param wbits The wbits parameter for zlib (use the ZFormat enum)
     */
    Decompressor(int wbits) {
        ::memset(&mStream, 0, sizeof(mStream));
        mInitialized = (::inflateInit2(&mStream, wbits) == Z_OK);
    }

    Decompressor(const Decompressor&) = delete;

    ~Decompressor() {
        if (mInitialized) {
            ::inflateEnd(&mStream);
        }
    }

    /**
     * @brief Doing the decompression on an async stream
     * 
     * @tparam T rquires Readable
     * @param output The output buffer for writing the decompressed data
     * @param source The source stream for reading the data, which need to be decompressed
     * @return Task<size_t> (The number of bytes written to the output buffer, 0 on EOF)
     */
    template <Readable T>
    auto decompressTo(std::span<std::byte> output, T &source) -> Task<size_t> {
        if (mStreamEnd) {
            co_return 0; //< EOF
        }
        mStream.next_out = (Bytef*) output.data();
        mStream.avail_out = output.size_bytes();

        if (mStream.avail_in == 0) {
            // We need to fill the source buffer
            if (mBuffer.empty()) {
                mBuffer.resize(1024);
            }
            if (mBufferFullFilled) { //< Trying to increase the buffer size, improve the performance
                mBufferFullFilled = false;
                mBuffer.resize(mBuffer.size() * 2);
            }
            auto n = co_await source.read(makeBuffer(mBuffer));
            if (!n || *n == 0) { //< Error from lower layer or lower layer return 0, We can not continue
                ILIAS_ERROR("Zlib", "Failed to read data from source stream");
                co_return Unexpected(n.error_or(Error::ZeroReturn));
            }
            mStream.next_in = (Bytef*) mBuffer.data();
            mStream.avail_in = *n;
            mBufferFullFilled = (*n == mBuffer.size());
        }
        do {
            auto ret = ::inflate(&mStream, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                ILIAS_ERROR("Zlib", "inflate error: {}", mStream.msg);
                co_return Unexpected(ZError(ret));
            }
            if (ret == Z_STREAM_END) {
                mStreamEnd = true;
                break;
            }
        } // Output buffer is full or source buffer is empty or end of stream
        while (mStream.avail_out != 0 && mStream.avail_in != 0);
        auto readed = mStream.next_out - (Bytef*) output.data(); //< Calculate the number of bytes readed
        co_return readed;
    }

    /**
     * @brief Check the decompressor is initialized
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mInitialized;
    }
private:
    ::z_stream mStream {};
    bool mInitialized = false;
    std::vector<std::byte> mBuffer {}; //< Buffer for the source, waiting to be decompressed
    bool mBufferFullFilled = false; //< Does the previous action make the buffer full filled ?
    bool mStreamEnd = false; //< Does the stream end ?
};

/**
 * @brief decompress a byte buffer using zlib
 * 
 * @tparam T Output buffer type
 * @param input The buffer to decompress
 * @param wbits The wbits parameter for zlib (use the ZFormat enum)
 * @return auto 
 */
template <MemWritable T = std::vector<std::byte> >
inline auto decompress(std::span<const std::byte> input, int wbits) -> Result<T> {
    static_assert(sizeof(std::declval<T>().data()[0]) == sizeof(Bytef), "Output buffer type must be a byte buffer");
    
    ::z_stream stream {};
    ::memset(&stream, 0, sizeof(stream));
    if (auto err = ::inflateInit2(&stream, wbits); err != Z_OK) {
        return Unexpected(ZError(err));
    }
    auto inPtr = (Bytef*) input.data();
    auto inSize = input.size_bytes();

    int ret = 0;
    T buffer;
    buffer.resize(inSize); //< Preallocate memory
    stream.avail_in = inSize;
    stream.next_in = inPtr;
    size_t pos = 0; //< Current posiiton
    do {
        stream.avail_out = buffer.size() - pos;
        stream.next_out = (Bytef*) buffer.data() + pos;
        do {
            ret = ::inflate(&stream, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                ::inflateEnd(&stream);
                return Unexpected(ZError(ret));
            }
        }
        while (stream.avail_out > 0 && ret != Z_STREAM_END);
        // Check buffer is full
        if (ret != Z_STREAM_END) {
            pos = stream.total_out;
            buffer.resize(buffer.size() * 2); //< Double buffer size
        }
    }
    while (ret != Z_STREAM_END);
    buffer.resize(stream.total_out);
    ::inflateEnd(&stream);
    return buffer;
}

/**
 * @brief Decompress a string using zlib
 * 
 * @param input The input string view
 * @param wbits The wbits parameter for zlib (use the ZFormat enum)
 * @return Result<std::string> 
 */
inline auto decompress(std::string_view input, int wbits) -> Result<std::string> {
    return decompress<std::string>(
        std::as_bytes(std::span(input)),
        wbits
    );
}


} // namespace zlib

ILIAS_NS_END

#else
    #define ILIAS_NO_ZLIB
#endif 