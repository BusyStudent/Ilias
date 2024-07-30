#pragma once

#include "../../ilias.hpp"

#include <span>
#include <memory>
#include <vector>

ILIAS_NS_BEGIN
namespace http2::detail {

struct IntegerEncoder {
    /**
     * @brief encode a integer to buffer
     * Pseudocode to represent an integer I is as follows:
     * if I < 2^N - 1, encode I on N bits
     * else
     *     encode (2^N - 1) on N bits
     *     I = I - (2^N - 1)
     *     while I >= 128
     *          encode (I % 128 + 128) on 8 bits
     *          I = I / 128
     *     encode I on 8 bits
     * @tparam T
     * @param value
     * @param outputBuffer
     * @param bitsOffset
     * @return int
     */
    template <typename T>
    static int encode(T &&value, std::vector<std::byte> &outputBuffer, const uint8_t bitsOffset = 0);
};

struct IntegerDecoder {
    /**
     * @brief read a integer from buffer
     * Integers are used to represent name indexes, header field indexes, or
     * string lengths.  An integer representation can start anywhere within
     * an octet.  To allow for optimized processing, an integer
     * representation always finishes at the end of an octet.
     * An integer is represented in two parts: a prefix that fills the
     * current octet and an optional list of octets that are used if the
     * integer value does not fit within the prefix.  The number of bits of
     * the prefix (called N) is a parameter of the integer representation.
     * If the integer value is small enough, i.e., strictly less than 2^N-1,
     * it is encoded within the N-bit prefix.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | ? | ? | ? |       Value       |
     * +---+---+---+-------------------+
     * Figure 2: Integer Value Encoded within the Prefix (Shown for N = 5)
     * Otherwise, all the bits of the prefix are set to 1, and the value,
     * decreased by 2^N-1, is encoded using a list of one or more octets.
     * The most significant bit of each octet is used as a continuation
     * flag: its value is set to 1 except for the last octet in the list.
     * The remaining bits of the octets are used to encode the decreased
     * value.
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * | ? | ? | ? | 1   1   1   1   1 |
     * +---+---+---+-------------------+
     * | 1 |    Value-(2^N-1) LSB      |
     * +---+---------------------------+
     *                ...
     * +---+---------------------------+
     * | 0 |    Value-(2^N-1) MSB      |
     * +---+---------------------------+
     * Figure 3: Integer Value Encoded after the Prefix (Shown for N = 5)
     * Pseudocode to decode an integer I is as follows:
     * decode I from the next N bits
     * if I < 2^N - 1, return I
     * else
     *     M = 0
     *     repeat
     *         B = next octet
     *         I = I + (B & 127) * 2^M
     *         M = M + 7
     *     while B & 128 == 128
     *     return I
     * @return int the next position if successful, -1 otherwise
     */
    template <typename T>
    static int decode(std::span<const std::byte> buffer, T &value, const uint8_t bitsOffset = 0);
};

template <typename T>
inline int IntegerEncoder::encode(T &&value, std::vector<std::byte> &outputBuffer, const uint8_t bitsOffset) {
    ILIAS_ASSERT(bitsOffset < 8 && bitsOffset >= 0);
    if (outputBuffer.empty()) {
        outputBuffer.emplace_back(static_cast<std::byte>(0));
    }
    std::byte b {static_cast<unsigned char>((1U << (8 - bitsOffset)) - 1U)};
    if (value < static_cast<unsigned char>(b)) {
        outputBuffer.back() |= static_cast<std::byte>(value);
    }
    else {
        outputBuffer.back() |= b;
        auto remain = value - static_cast<unsigned char>(b);
        while (remain > 0x7F) {
            auto r = remain % 0x80;
            r += 0x80;
            outputBuffer.push_back(static_cast<std::byte>(r));
            remain /= 0x80;
        }
        outputBuffer.push_back(static_cast<std::byte>(remain));
    }
    return 0;
}

template <typename T>
inline int IntegerDecoder::decode(std::span<const std::byte> buffer, T &value, const uint8_t bitsOffset) {
    ILIAS_ASSERT(bitsOffset < 8 && bitsOffset >= 0);
    ILIAS_ASSERT(buffer.size() > 0);

    std::byte b {static_cast<unsigned char>((1U << (8 - bitsOffset)) - 1U)};
    if (static_cast<unsigned char>(buffer[0] & b) < static_cast<unsigned char>(b)) {
        value = static_cast<T>(buffer[0] & b);
        return 1;
    }
    int current = 1, valueBitsOffset = 0;
    value = 0;
    while (current < buffer.size() && (static_cast<unsigned char>(buffer[current]) & 0b10000000U)) {
        value |= static_cast<T>(static_cast<unsigned char>(buffer[current]) & 0b01111111U) << valueBitsOffset;
        valueBitsOffset += 7;
        if (valueBitsOffset + 7 > sizeof(T) * 8) {
            return -1;
        }
        ++current;
    }
    if (current < buffer.size()) {
        value |= static_cast<T>(static_cast<unsigned char>(buffer[current]) & 0b01111111U) << valueBitsOffset;
        value += static_cast<unsigned char>(b);
        return current + 1;
    }
    // FIXME: if current == buffer.size() then is error ?
    return -2;
}

} // namespace http2::detail
ILIAS_NS_END