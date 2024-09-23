#pragma once

#include <ilias/ilias.hpp>

#include <memory>
#include <vector>
#include <span>

#define HTTP_2_HPACK_HUFFMAN_CODE_EOS 256

ILIAS_NS_BEGIN
namespace http2::detail {

struct HuffmanCode {
    int         rawCode    = {};
    uint32_t    encode     = {};
    std::size_t encodeBits = {};

    inline int leastBytes() const { return (encodeBits + 7) / 8; }
    /**
     * @brief write huffmanCode to byteBuffer and calc new byteOffset and bitsOffset
     *
     * @param[out] byteBuffer output buffer
     * @param[in] byteOffset the byte offset for current buffer
     * @param[in,out] bitsOffset the bits offset for current byte
     * @return int new byteOffset after write this huffman code if success, -1 otherwise.
     */
};

class HuffmanEncoder {
public:
    static int encode(std::span<const std::byte> byteBuffer, std::vector<std::byte> &outputBuffer);
    static int encodeOne(std::vector<std::byte> &outputBuffer, const uint8_t bitsOffset, unsigned char code);

private:
    static constexpr const std::array<HuffmanCode, 257> kStaticHuffmanCode =
#include "static_huffmancode_encoder.inc"
        ; // static huffman code table for encoder
};

class HuffmanDecoder {
public:
    /// @brief Decode huffman->buffer
    /// @param huffman The buffman data to be decoded
    /// @param buffer Decoded buffer data
    /// @param inout_state On input, it means the end state of the last decoding; on output, it means the state of the
    /// current decoding. The default state is 0. This value is used to decode incomplete huffman packets consecutively,
    /// nullptr is allowed for complete huffman packets.
    /// @return Decoded data length
    static int decode(std::span<const std::byte> huffman, std::vector<std::byte> &buffer,
                      int16_t *inout_state = nullptr);

private:
    struct HuffmanNode {
        int16_t value    = {-1};
        int16_t parent   = {-1};
        int16_t child[2] = {-1, -1};

        auto operator<=>(const HuffmanNode &right) const = default;
    };

    static constexpr HuffmanNode huffman_nodes[] =
#include "static_huffmancode_decoder.inc"
        ; // static huffman code table for decoder
};

inline int HuffmanEncoder::encode(std::span<const std::byte> byteBuffer, std::vector<std::byte> &outputBuffer) {
    int bitsOffset = 0;
    for (auto byte : byteBuffer) {
        bitsOffset = encodeOne(outputBuffer, bitsOffset, static_cast<unsigned char>(byte));
    }
    if (bitsOffset != 0) {
        auto eos = kStaticHuffmanCode[HTTP_2_HPACK_HUFFMAN_CODE_EOS];
        outputBuffer.back() ^= static_cast<std::byte>(eos.encode) >> bitsOffset;
    }
    return 0;
}
inline int HuffmanEncoder::encodeOne(std::vector<std::byte> &outputBuffer, const uint8_t bitsOffset,
                                     unsigned char code) {
    ILIAS_ASSERT(bitsOffset < 8);

    auto huffmanCode = kStaticHuffmanCode[code];
    // |11111111|11111111|01010
    // like this, we need to copy 2 bytes and remain 4 bits.
    int     copyBytes = huffmanCode.encodeBits / 8;
    uint8_t remainBits =
        huffmanCode.encodeBits - copyBytes * 8; // this huffmanCode remain bits after copy bytes as much as possible.
    for (int i = copyBytes - 1; i >= 0; --i) {
        // |11111111|11111111|01010 make the highest 8-digit valid value to the lowest digit.
        uint8_t hc = (huffmanCode.encode >> (i * 8 + remainBits)) & 0xff;
        // if bitsOffset > 0, we need to merge the previous value
        if (bitsOffset > 0) {
            outputBuffer.back() ^= std::byte(hc >> bitsOffset);        // merge the previous value.
            outputBuffer.push_back(std::byte(hc << (8 - bitsOffset))); // write remain valid bits to next byte.
        }
        else {
            outputBuffer.push_back(std::byte(hc)); // write bits in next byte.
        }
    }
    // remain less than 8 bits
    if (remainBits > 0) {
        uint8_t hc = (huffmanCode.encode & 0xff)
                     << (8 - remainBits); // make the lowest remain valid digit to the highest digit
        if (bitsOffset > 0) {
            outputBuffer.back() ^= std::byte(hc >> bitsOffset); // merge the previous value
            if (8 < remainBits + bitsOffset) {
                // if remainBits + bitsOffset > 8, Then there is still some data that is deposited
                // we need to write it to the next byte
                outputBuffer.push_back(std::byte(hc << (8 - bitsOffset)));
                return remainBits + bitsOffset - 8; // then this byte will remain 8 - (remainBits + bitsOffset) % 8
                                                    // bits, offset is remainBits + bitsOffset - 8.
            }
            // or remainBits + bitsOffset <= 8, then we have finished write all bits
            // then this byte will remain 8 - remainBits - bitsOffset bits, the offset is 8 - remain bits. and it
            // must less then 8.
            return remainBits + bitsOffset >= 8 ? 0 : remainBits + bitsOffset;
        }
        else {
            outputBuffer.push_back(std::byte(hc)); // write bits in next byte.
            return remainBits;                     // then this byte will remain 8 - remainBits.
        }
    }
    return bitsOffset;
}

inline int HuffmanDecoder::decode(std::span<const std::byte> huffman, std::vector<std::byte> &buffer,
                                  int16_t *inout_state) {
    int16_t cur  = {}; // 当前所在的 HuffmanTree 中的节点
    size_t  cnt  = 0;  // 可以解析的内容的计数器
    uint8_t mask = 0;

    cur = inout_state != nullptr ? *inout_state : 0;
    for (size_t i = 0; i < huffman.size(); ++i) {
        mask = 128u; // 1<<7
        for (mask = 128u; mask; mask >>= 1) {
            cur = huffman_nodes[cur].child[static_cast<uint8_t>(huffman[i]) & mask ? 1 : 0];
            if (cur == -1)
                return static_cast<int>(i);
            if (huffman_nodes[cur].value != -1)
                ++cnt, cur = 0;
        }
    }

    size_t old_size = buffer.size();
    buffer.resize(buffer.size() + cnt);

    cur = inout_state != nullptr ? *inout_state : 0;
    for (size_t i = 0; i < huffman.size(); ++i) {
        mask = 128u; // 1<<7
        for (mask = 128u; mask; mask >>= 1) {
            cur = huffman_nodes[cur].child[static_cast<uint8_t>(huffman[i]) & mask ? 1 : 0];
            if (huffman_nodes[cur].value != -1)
                buffer[old_size++] = static_cast<std::byte>(huffman_nodes[cur].value), cur = 0;
        }
    }

    if (inout_state != nullptr)
        *inout_state = cur;
    return static_cast<int>(huffman.size());
}

} // namespace http2::detail

ILIAS_NS_END