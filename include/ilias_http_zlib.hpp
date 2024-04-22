#pragma once

#if __has_include(<zlib.h>) && !defined(ILIAS_NO_ZLIB)

#include "ilias.hpp"
#include <zlib.h>
#include <vector>
#include <span>

ILIAS_NS_BEGIN

namespace Zlib {

/**
 * @brief Decompress the zlib byte stream by format
 * 
 * @tparam T 
 * @tparam U 
 * @param input 
 * @return T 
 */
template <typename T = std::vector<uint8_t> >
inline auto decompress(std::span<const uint8_t> input, int wbits) -> T {
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
inline auto decompress(std::string_view input, int wbits) -> std::string {
    return decompress<std::string>(
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(input.data()),
            input.size()
        ),
        wbits
    );
}

}


namespace Gzip {

inline auto decompress(std::string_view input) -> std::string {
    return Zlib::decompress(input, 16 + MAX_WBITS);
}

}

namespace Deflate {

inline auto decompress(std::string_view input) -> std::string {
    return Zlib::decompress(input, -MAX_WBITS);
}

}

ILIAS_NS_END

#else
#define ILIAS_NO_ZLIB
#endif