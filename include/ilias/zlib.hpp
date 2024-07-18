#pragma once

#if __has_include(<zlib.h>) && !defined(ILIAS_NO_ZLIB)

#include "expected.hpp"
#include "ilias.hpp"
#include <cstring>
#include <zlib.h>
#include <vector>
#include <span>

ILIAS_NS_BEGIN

namespace Zlib {

/**
 * @brief Output container
 * 
 * @tparam T 
 */
template <typename T>
concept Output = requires(T t) {
    t.size();
    t.data();
    t.resize(size_t { }); //< Allow resize
};

/**
 * @brief The format
 * 
 */
enum ZFormat : int {
    GzipFormat = 16 + MAX_WBITS,
    DeflateFormat = MAX_WBITS,
};

/**
 * @brief The zlib err
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

    auto message(uint32_t ev) const -> std::string override {
        switch (int(ev)) {
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

    /**
     * @brief Make zlib error into Error object
     * 
     * @param err 
     * @return Error 
     */
    static auto makeError(int err) -> Error {
        return Error(err, instance());
    }
};


// TODO 
/**
 * @brief for decompress and compress like read write a stream
 * 
 */
class ZStream {
public:
    using enum ZFormat;

    ZStream() = default;
    ZStream(const ZStream &) = delete;
    ~ZStream() { close(); }

    /**
     * @brief Close the stream, user can reinit it
     * 
     */
    auto close() -> void {
        if (mEnd) {
            mEnd(&mStream);
        }
        ::memset(&mStream, 0, sizeof(mStream));
        mInputBuffer.clear();
        mProcess = nullptr;
        mEnd = nullptr;
    }

    /**
     * @brief Initialize the stream to decompress
     * 
     * @param wbits 
     * @return Result<> 
     */
    auto initDecompress(int wbits) -> Result<> {
        ILIAS_ASSERT(!isInited());
        auto err = ::inflateInit2(&mStream, wbits);
        if (err != Z_OK) {
            return Unexpected(ZCategory::makeError(err));
        }
        mEnd = ::inflateEnd;
        mProcess = ::inflate;
        return {};
    }

    /**
     * @brief Check the stream is
     * 
     * @return true 
     * @return false 
     */
    auto isInited() const -> bool {
        return mEnd != nullptr;
    }

    /**
     * @brief Write num of data into it, waiting for process
     * 
     * @param input 
     * @return Result<size_t> 
     */
    auto write(std::span<const std::byte> input) -> Result<size_t> {
        ILIAS_ASSERT(isInited());
        mInputBuffer.insert(mInputBuffer.end(), input.begin(), input.end());
        return input.size_bytes();
    }
private:
    ::z_stream mStream { };
    int (*mEnd)(::z_stream *) = nullptr; //< Cleanup
    int (*mProcess)(::z_stream *, int flush) = nullptr; //< Used in read
    std::vector<std::byte> mInputBuffer; 
};


/**
 * @brief Decompress the zlib byte stream by format
 * 
 * @tparam T 
 * @tparam U 
 * @param input 
 * @return T 
 */
template <Output T = std::vector<std::byte> >
inline auto decompress(std::span<const std::byte> input, int wbits) -> T {
    ::z_stream stream {};
    ::memset(&stream, 0, sizeof(stream));
    if (::inflateInit2(&stream, wbits) != Z_OK) {
        return T();
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
                return T();
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
 * @brief Decompress the input data by format
 * 
 * @param input 
 * @param wbits The format
 * @return std::string 
 */
inline auto decompress(std::string_view input, int wbits) -> std::string {
    return decompress<std::string>(
        std::as_bytes(std::span(input.data(), input.size())),
        wbits
    );
}

using Zlib::ZStream; //< Export to it

}

ILIAS_NS_END

#else
#define ILIAS_NO_ZLIB
#endif