#pragma once

#include "../../ilias.hpp"

#include <span>
#include <memory>

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
    static inline int encode(std::span<const std::byte> byteBuffer, std::vector<std::byte> &outputBuffer) {
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
    static inline int encodeOne(std::vector<std::byte> &outputBuffer, const uint8_t bitsOffset, unsigned char code) {
        ILIAS_ASSERT(bitsOffset < 8);

        auto huffmanCode = kStaticHuffmanCode[code];
        // |11111111|11111111|01010
        // like this, we need to copy 2 bytes and remain 4 bits.
        int     copyBytes  = huffmanCode.encodeBits / 8;
        uint8_t remainBits = huffmanCode.encodeBits -
                             copyBytes * 8; // this huffmanCode remain bits after copy bytes as much as possible.
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

private:
    static constexpr const std::array<HuffmanCode, 257> kStaticHuffmanCode = {
        HuffmanCode {0, 0x1ff8, 13},      HuffmanCode {1, 0x7fffd8, 23},    HuffmanCode {2, 0xfffffe2, 28},
        HuffmanCode {3, 0xfffffe3, 28},   HuffmanCode {4, 0xfffffe4, 28},   HuffmanCode {5, 0xfffffe5, 28},
        HuffmanCode {6, 0xfffffe6, 28},   HuffmanCode {7, 0xfffffe7, 28},   HuffmanCode {8, 0xfffffe8, 28},
        HuffmanCode {9, 0xffffea, 24},    HuffmanCode {10, 0x3ffffffc, 30}, HuffmanCode {11, 0xfffffe9, 28},
        HuffmanCode {12, 0xfffffea, 28},  HuffmanCode {13, 0x3ffffffd, 30}, HuffmanCode {14, 0xfffffeb, 28},
        HuffmanCode {15, 0xfffffec, 28},  HuffmanCode {16, 0xfffffed, 28},  HuffmanCode {17, 0xfffffee, 28},
        HuffmanCode {18, 0xfffffef, 28},  HuffmanCode {19, 0xffffff0, 28},  HuffmanCode {20, 0xffffff1, 28},
        HuffmanCode {21, 0xffffff2, 28},  HuffmanCode {22, 0x3ffffffe, 30}, HuffmanCode {23, 0xffffff3, 28},
        HuffmanCode {24, 0xffffff4, 28},  HuffmanCode {25, 0xffffff5, 28},  HuffmanCode {26, 0xffffff6, 28},
        HuffmanCode {27, 0xffffff7, 28},  HuffmanCode {28, 0xffffff8, 28},  HuffmanCode {29, 0xffffff9, 28},
        HuffmanCode {30, 0xffffffa, 28},  HuffmanCode {31, 0xffffffb, 28},  HuffmanCode {32, 0x14, 6},
        HuffmanCode {33, 0x3f8, 10},      HuffmanCode {34, 0x3f9, 10},      HuffmanCode {35, 0xffa, 12},
        HuffmanCode {36, 0x1ff9, 13},     HuffmanCode {37, 0x15, 6},        HuffmanCode {38, 0xf8, 8},
        HuffmanCode {39, 0x7fa, 11},      HuffmanCode {40, 0x3fa, 10},      HuffmanCode {41, 0x3fb, 10},
        HuffmanCode {42, 0xf9, 8},        HuffmanCode {43, 0x7fb, 11},      HuffmanCode {44, 0xfa, 8},
        HuffmanCode {45, 0x16, 6},        HuffmanCode {46, 0x17, 6},        HuffmanCode {47, 0x18, 6},
        HuffmanCode {48, 0x0, 5},         HuffmanCode {49, 0x1, 5},         HuffmanCode {50, 0x2, 5},
        HuffmanCode {51, 0x19, 6},        HuffmanCode {52, 0x1a, 6},        HuffmanCode {53, 0x1b, 6},
        HuffmanCode {54, 0x1c, 6},        HuffmanCode {55, 0x1d, 6},        HuffmanCode {56, 0x1e, 6},
        HuffmanCode {57, 0x1f, 6},        HuffmanCode {58, 0x5c, 7},        HuffmanCode {59, 0xfb, 8},
        HuffmanCode {60, 0x7ffc, 15},     HuffmanCode {61, 0x20, 6},        HuffmanCode {62, 0xffb, 12},
        HuffmanCode {63, 0x3fc, 10},      HuffmanCode {64, 0x1ffa, 13},     HuffmanCode {65, 0x21, 6},
        HuffmanCode {66, 0x5d, 7},        HuffmanCode {67, 0x5e, 7},        HuffmanCode {68, 0x5f, 7},
        HuffmanCode {69, 0x60, 7},        HuffmanCode {70, 0x61, 7},        HuffmanCode {71, 0x62, 7},
        HuffmanCode {72, 0x63, 7},        HuffmanCode {73, 0x64, 7},        HuffmanCode {74, 0x65, 7},
        HuffmanCode {75, 0x66, 7},        HuffmanCode {76, 0x67, 7},        HuffmanCode {77, 0x68, 7},
        HuffmanCode {78, 0x69, 7},        HuffmanCode {79, 0x6a, 7},        HuffmanCode {80, 0x6b, 7},
        HuffmanCode {81, 0x6c, 7},        HuffmanCode {82, 0x6d, 7},        HuffmanCode {83, 0x6e, 7},
        HuffmanCode {84, 0x6f, 7},        HuffmanCode {85, 0x70, 7},        HuffmanCode {86, 0x71, 7},
        HuffmanCode {87, 0x72, 7},        HuffmanCode {88, 0xfc, 8},        HuffmanCode {89, 0x73, 7},
        HuffmanCode {90, 0xfd, 8},        HuffmanCode {91, 0x1ffb, 13},     HuffmanCode {92, 0x7fff0, 19},
        HuffmanCode {93, 0x1ffc, 13},     HuffmanCode {94, 0x3ffc, 14},     HuffmanCode {95, 0x22, 6},
        HuffmanCode {96, 0x7ffd, 15},     HuffmanCode {97, 0x3, 5},         HuffmanCode {98, 0x23, 6},
        HuffmanCode {99, 0x4, 5},         HuffmanCode {100, 0x24, 6},       HuffmanCode {101, 0x5, 5},
        HuffmanCode {102, 0x25, 6},       HuffmanCode {103, 0x26, 6},       HuffmanCode {104, 0x27, 6},
        HuffmanCode {105, 0x6, 5},        HuffmanCode {106, 0x74, 7},       HuffmanCode {107, 0x75, 7},
        HuffmanCode {108, 0x28, 6},       HuffmanCode {109, 0x29, 6},       HuffmanCode {110, 0x2a, 6},
        HuffmanCode {111, 0x7, 5},        HuffmanCode {112, 0x2b, 6},       HuffmanCode {113, 0x76, 7},
        HuffmanCode {114, 0x2c, 6},       HuffmanCode {115, 0x8, 5},        HuffmanCode {116, 0x9, 5},
        HuffmanCode {117, 0x2d, 6},       HuffmanCode {118, 0x77, 7},       HuffmanCode {119, 0x78, 7},
        HuffmanCode {120, 0x79, 7},       HuffmanCode {121, 0x7a, 7},       HuffmanCode {122, 0x7b, 7},
        HuffmanCode {123, 0x7ffe, 15},    HuffmanCode {124, 0x7fc, 11},     HuffmanCode {125, 0x3ffd, 14},
        HuffmanCode {126, 0x1ffd, 13},    HuffmanCode {127, 0xffffffc, 28}, HuffmanCode {128, 0xfffe6, 20},
        HuffmanCode {129, 0x3fffd2, 22},  HuffmanCode {130, 0xfffe7, 20},   HuffmanCode {131, 0xfffe8, 20},
        HuffmanCode {132, 0x3fffd3, 22},  HuffmanCode {133, 0x3fffd4, 22},  HuffmanCode {134, 0x3fffd5, 22},
        HuffmanCode {135, 0x7fffd9, 23},  HuffmanCode {136, 0x3fffd6, 22},  HuffmanCode {137, 0x7fffda, 23},
        HuffmanCode {138, 0x7fffdb, 23},  HuffmanCode {139, 0x7fffdc, 23},  HuffmanCode {140, 0x7fffdd, 23},
        HuffmanCode {141, 0x7fffde, 23},  HuffmanCode {142, 0xffffeb, 24},  HuffmanCode {143, 0x7fffdf, 23},
        HuffmanCode {144, 0xffffec, 24},  HuffmanCode {145, 0xffffed, 24},  HuffmanCode {146, 0x3fffd7, 22},
        HuffmanCode {147, 0x7fffe0, 23},  HuffmanCode {148, 0xffffee, 24},  HuffmanCode {149, 0x7fffe1, 23},
        HuffmanCode {150, 0x7fffe2, 23},  HuffmanCode {151, 0x7fffe3, 23},  HuffmanCode {152, 0x7fffe4, 23},
        HuffmanCode {153, 0x1fffdc, 21},  HuffmanCode {154, 0x3fffd8, 22},  HuffmanCode {155, 0x7fffe5, 23},
        HuffmanCode {156, 0x3fffd9, 22},  HuffmanCode {157, 0x7fffe6, 23},  HuffmanCode {158, 0x7fffe7, 23},
        HuffmanCode {159, 0xffffef, 24},  HuffmanCode {160, 0x3fffda, 22},  HuffmanCode {161, 0x1fffdd, 21},
        HuffmanCode {162, 0xfffe9, 20},   HuffmanCode {163, 0x3fffdb, 22},  HuffmanCode {164, 0x3fffdc, 22},
        HuffmanCode {165, 0x7fffe8, 23},  HuffmanCode {166, 0x7fffe9, 23},  HuffmanCode {167, 0x1fffde, 21},
        HuffmanCode {168, 0x7fffea, 23},  HuffmanCode {169, 0x3fffdd, 22},  HuffmanCode {170, 0x3fffde, 22},
        HuffmanCode {171, 0xfffff0, 24},  HuffmanCode {172, 0x1fffdf, 21},  HuffmanCode {173, 0x3fffdf, 22},
        HuffmanCode {174, 0x7fffeb, 23},  HuffmanCode {175, 0x7fffec, 23},  HuffmanCode {176, 0x1fffe0, 21},
        HuffmanCode {177, 0x1fffe1, 21},  HuffmanCode {178, 0x3fffe0, 22},  HuffmanCode {179, 0x1fffe2, 21},
        HuffmanCode {180, 0x7fffed, 23},  HuffmanCode {181, 0x3fffe1, 22},  HuffmanCode {182, 0x7fffee, 23},
        HuffmanCode {183, 0x7fffef, 23},  HuffmanCode {184, 0xfffea, 20},   HuffmanCode {185, 0x3fffe2, 22},
        HuffmanCode {186, 0x3fffe3, 22},  HuffmanCode {187, 0x3fffe4, 22},  HuffmanCode {188, 0x7ffff0, 23},
        HuffmanCode {189, 0x3fffe5, 22},  HuffmanCode {190, 0x3fffe6, 22},  HuffmanCode {191, 0x7ffff1, 23},
        HuffmanCode {192, 0x3ffffe0, 26}, HuffmanCode {193, 0x3ffffe1, 26}, HuffmanCode {194, 0xfffeb, 20},
        HuffmanCode {195, 0x7fff1, 19},   HuffmanCode {196, 0x3fffe7, 22},  HuffmanCode {197, 0x7ffff2, 23},
        HuffmanCode {198, 0x3fffe8, 22},  HuffmanCode {199, 0x1ffffec, 25}, HuffmanCode {200, 0x3ffffe2, 26},
        HuffmanCode {201, 0x3ffffe3, 26}, HuffmanCode {202, 0x3ffffe4, 26}, HuffmanCode {203, 0x7ffffde, 27},
        HuffmanCode {204, 0x7ffffdf, 27}, HuffmanCode {205, 0x3ffffe5, 26}, HuffmanCode {206, 0xfffff1, 24},
        HuffmanCode {207, 0x1ffffed, 25}, HuffmanCode {208, 0x7fff2, 19},   HuffmanCode {209, 0x1fffe3, 21},
        HuffmanCode {210, 0x3ffffe6, 26}, HuffmanCode {211, 0x7ffffe0, 27}, HuffmanCode {212, 0x7ffffe1, 27},
        HuffmanCode {213, 0x3ffffe7, 26}, HuffmanCode {214, 0x7ffffe2, 27}, HuffmanCode {215, 0xfffff2, 24},
        HuffmanCode {216, 0x1fffe4, 21},  HuffmanCode {217, 0x1fffe5, 21},  HuffmanCode {218, 0x3ffffe8, 26},
        HuffmanCode {219, 0x3ffffe9, 26}, HuffmanCode {220, 0xffffffd, 28}, HuffmanCode {221, 0x7ffffe3, 27},
        HuffmanCode {222, 0x7ffffe4, 27}, HuffmanCode {223, 0x7ffffe5, 27}, HuffmanCode {224, 0xfffec, 20},
        HuffmanCode {225, 0xfffff3, 24},  HuffmanCode {226, 0xfffed, 20},   HuffmanCode {227, 0x1fffe6, 21},
        HuffmanCode {228, 0x3fffe9, 22},  HuffmanCode {229, 0x1fffe7, 21},  HuffmanCode {230, 0x1fffe8, 21},
        HuffmanCode {231, 0x7ffff3, 23},  HuffmanCode {232, 0x3fffea, 22},  HuffmanCode {233, 0x3fffeb, 22},
        HuffmanCode {234, 0x1ffffee, 25}, HuffmanCode {235, 0x1ffffef, 25}, HuffmanCode {236, 0xfffff4, 24},
        HuffmanCode {237, 0xfffff5, 24},  HuffmanCode {238, 0x3ffffea, 26}, HuffmanCode {239, 0x7ffff4, 23},
        HuffmanCode {240, 0x3ffffeb, 26}, HuffmanCode {241, 0x7ffffe6, 27}, HuffmanCode {242, 0x3ffffec, 26},
        HuffmanCode {243, 0x3ffffed, 26}, HuffmanCode {244, 0x7ffffe7, 27}, HuffmanCode {245, 0x7ffffe8, 27},
        HuffmanCode {246, 0x7ffffe9, 27}, HuffmanCode {247, 0x7ffffea, 27}, HuffmanCode {248, 0x7ffffeb, 27},
        HuffmanCode {249, 0xffffffe, 28}, HuffmanCode {250, 0x7ffffec, 27}, HuffmanCode {251, 0x7ffffed, 27},
        HuffmanCode {252, 0x7ffffee, 27}, HuffmanCode {253, 0x7ffffef, 27}, HuffmanCode {254, 0x7fffff0, 27},
        HuffmanCode {255, 0x3ffffee, 26}, HuffmanCode {256, 0x3fffffff, 30}};
};

class HuffmanDecoder {
public:
    /// @brief 编码 buffer->huffman
    /// @param buffer 需要编码的 buffer 数据
    /// @param huffman 编码后的 huffman 数据
    /// @param inout_byte 0 <= inout_byte <= 2^7，且一定是 2 的幂次。输入时表示：上次填充到末尾 byte 的哪个 bit
    /// 位；编码完成时表示，填充到末尾 byte 的哪个 bit位。当 ending=1 时，输出一定是 0。此值用于连续编码不完整的 buffer
    /// 数据包，对于完整的 buffer 数据包允许使用 nullptr
    /// @param ending 是否使用 1 填充结尾以结束编码
    /// @return 已解析的长度
    inline static int decode(std::span<const std::byte> huffman, std::vector<std::byte> &buffer,
                             int16_t *inout_state = nullptr) {
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

private:
    struct HuffmanNode {
        int16_t value    = {-1};
        int16_t parent   = {-1};
        int16_t child[2] = {-1, -1};

        auto operator<=>(const HuffmanNode &right) const = default;
    };

    static constexpr HuffmanNode huffman_nodes[] = {
        HuffmanNode(-1, -1, {1, 44}),     HuffmanNode(-1, 0, {2, 17}),      HuffmanNode(-1, 1, {3, 10}),
        HuffmanNode(-1, 2, {4, 7}),       HuffmanNode(-1, 3, {5, 6}),       HuffmanNode(48, 4, {-1, -1}),
        HuffmanNode(49, 4, {-1, -1}),     HuffmanNode(-1, 3, {8, 9}),       HuffmanNode(50, 7, {-1, -1}),
        HuffmanNode(97, 7, {-1, -1}),     HuffmanNode(-1, 2, {11, 14}),     HuffmanNode(-1, 10, {12, 13}),
        HuffmanNode(99, 11, {-1, -1}),    HuffmanNode(101, 11, {-1, -1}),   HuffmanNode(-1, 10, {15, 16}),
        HuffmanNode(105, 14, {-1, -1}),   HuffmanNode(111, 14, {-1, -1}),   HuffmanNode(-1, 1, {18, 29}),
        HuffmanNode(-1, 17, {19, 22}),    HuffmanNode(-1, 18, {20, 21}),    HuffmanNode(115, 19, {-1, -1}),
        HuffmanNode(116, 19, {-1, -1}),   HuffmanNode(-1, 18, {23, 26}),    HuffmanNode(-1, 22, {24, 25}),
        HuffmanNode(32, 23, {-1, -1}),    HuffmanNode(37, 23, {-1, -1}),    HuffmanNode(-1, 22, {27, 28}),
        HuffmanNode(45, 26, {-1, -1}),    HuffmanNode(46, 26, {-1, -1}),    HuffmanNode(-1, 17, {30, 37}),
        HuffmanNode(-1, 29, {31, 34}),    HuffmanNode(-1, 30, {32, 33}),    HuffmanNode(47, 31, {-1, -1}),
        HuffmanNode(51, 31, {-1, -1}),    HuffmanNode(-1, 30, {35, 36}),    HuffmanNode(52, 34, {-1, -1}),
        HuffmanNode(53, 34, {-1, -1}),    HuffmanNode(-1, 29, {38, 41}),    HuffmanNode(-1, 37, {39, 40}),
        HuffmanNode(54, 38, {-1, -1}),    HuffmanNode(55, 38, {-1, -1}),    HuffmanNode(-1, 37, {42, 43}),
        HuffmanNode(56, 41, {-1, -1}),    HuffmanNode(57, 41, {-1, -1}),    HuffmanNode(-1, 0, {45, 80}),
        HuffmanNode(-1, 44, {46, 61}),    HuffmanNode(-1, 45, {47, 54}),    HuffmanNode(-1, 46, {48, 51}),
        HuffmanNode(-1, 47, {49, 50}),    HuffmanNode(61, 48, {-1, -1}),    HuffmanNode(65, 48, {-1, -1}),
        HuffmanNode(-1, 47, {52, 53}),    HuffmanNode(95, 51, {-1, -1}),    HuffmanNode(98, 51, {-1, -1}),
        HuffmanNode(-1, 46, {55, 58}),    HuffmanNode(-1, 54, {56, 57}),    HuffmanNode(100, 55, {-1, -1}),
        HuffmanNode(102, 55, {-1, -1}),   HuffmanNode(-1, 54, {59, 60}),    HuffmanNode(103, 58, {-1, -1}),
        HuffmanNode(104, 58, {-1, -1}),   HuffmanNode(-1, 45, {62, 69}),    HuffmanNode(-1, 61, {63, 66}),
        HuffmanNode(-1, 62, {64, 65}),    HuffmanNode(108, 63, {-1, -1}),   HuffmanNode(109, 63, {-1, -1}),
        HuffmanNode(-1, 62, {67, 68}),    HuffmanNode(110, 66, {-1, -1}),   HuffmanNode(112, 66, {-1, -1}),
        HuffmanNode(-1, 61, {70, 73}),    HuffmanNode(-1, 69, {71, 72}),    HuffmanNode(114, 70, {-1, -1}),
        HuffmanNode(117, 70, {-1, -1}),   HuffmanNode(-1, 69, {74, 77}),    HuffmanNode(-1, 73, {75, 76}),
        HuffmanNode(58, 74, {-1, -1}),    HuffmanNode(66, 74, {-1, -1}),    HuffmanNode(-1, 73, {78, 79}),
        HuffmanNode(67, 77, {-1, -1}),    HuffmanNode(68, 77, {-1, -1}),    HuffmanNode(-1, 44, {81, 112}),
        HuffmanNode(-1, 80, {82, 97}),    HuffmanNode(-1, 81, {83, 90}),    HuffmanNode(-1, 82, {84, 87}),
        HuffmanNode(-1, 83, {85, 86}),    HuffmanNode(69, 84, {-1, -1}),    HuffmanNode(70, 84, {-1, -1}),
        HuffmanNode(-1, 83, {88, 89}),    HuffmanNode(71, 87, {-1, -1}),    HuffmanNode(72, 87, {-1, -1}),
        HuffmanNode(-1, 82, {91, 94}),    HuffmanNode(-1, 90, {92, 93}),    HuffmanNode(73, 91, {-1, -1}),
        HuffmanNode(74, 91, {-1, -1}),    HuffmanNode(-1, 90, {95, 96}),    HuffmanNode(75, 94, {-1, -1}),
        HuffmanNode(76, 94, {-1, -1}),    HuffmanNode(-1, 81, {98, 105}),   HuffmanNode(-1, 97, {99, 102}),
        HuffmanNode(-1, 98, {100, 101}),  HuffmanNode(77, 99, {-1, -1}),    HuffmanNode(78, 99, {-1, -1}),
        HuffmanNode(-1, 98, {103, 104}),  HuffmanNode(79, 102, {-1, -1}),   HuffmanNode(80, 102, {-1, -1}),
        HuffmanNode(-1, 97, {106, 109}),  HuffmanNode(-1, 105, {107, 108}), HuffmanNode(81, 106, {-1, -1}),
        HuffmanNode(82, 106, {-1, -1}),   HuffmanNode(-1, 105, {110, 111}), HuffmanNode(83, 109, {-1, -1}),
        HuffmanNode(84, 109, {-1, -1}),   HuffmanNode(-1, 80, {113, 128}),  HuffmanNode(-1, 112, {114, 121}),
        HuffmanNode(-1, 113, {115, 118}), HuffmanNode(-1, 114, {116, 117}), HuffmanNode(85, 115, {-1, -1}),
        HuffmanNode(86, 115, {-1, -1}),   HuffmanNode(-1, 114, {119, 120}), HuffmanNode(87, 118, {-1, -1}),
        HuffmanNode(89, 118, {-1, -1}),   HuffmanNode(-1, 113, {122, 125}), HuffmanNode(-1, 121, {123, 124}),
        HuffmanNode(106, 122, {-1, -1}),  HuffmanNode(107, 122, {-1, -1}),  HuffmanNode(-1, 121, {126, 127}),
        HuffmanNode(113, 125, {-1, -1}),  HuffmanNode(118, 125, {-1, -1}),  HuffmanNode(-1, 112, {129, 136}),
        HuffmanNode(-1, 128, {130, 133}), HuffmanNode(-1, 129, {131, 132}), HuffmanNode(119, 130, {-1, -1}),
        HuffmanNode(120, 130, {-1, -1}),  HuffmanNode(-1, 129, {134, 135}), HuffmanNode(121, 133, {-1, -1}),
        HuffmanNode(122, 133, {-1, -1}),  HuffmanNode(-1, 128, {137, 144}), HuffmanNode(-1, 136, {138, 141}),
        HuffmanNode(-1, 137, {139, 140}), HuffmanNode(38, 138, {-1, -1}),   HuffmanNode(42, 138, {-1, -1}),
        HuffmanNode(-1, 137, {142, 143}), HuffmanNode(44, 141, {-1, -1}),   HuffmanNode(59, 141, {-1, -1}),
        HuffmanNode(-1, 136, {145, 148}), HuffmanNode(-1, 144, {146, 147}), HuffmanNode(88, 145, {-1, -1}),
        HuffmanNode(90, 145, {-1, -1}),   HuffmanNode(-1, 144, {149, 156}), HuffmanNode(-1, 148, {150, 153}),
        HuffmanNode(-1, 149, {151, 152}), HuffmanNode(33, 150, {-1, -1}),   HuffmanNode(34, 150, {-1, -1}),
        HuffmanNode(-1, 149, {154, 155}), HuffmanNode(40, 153, {-1, -1}),   HuffmanNode(41, 153, {-1, -1}),
        HuffmanNode(-1, 148, {157, 162}), HuffmanNode(-1, 156, {158, 159}), HuffmanNode(63, 157, {-1, -1}),
        HuffmanNode(-1, 157, {160, 161}), HuffmanNode(39, 159, {-1, -1}),   HuffmanNode(43, 159, {-1, -1}),
        HuffmanNode(-1, 156, {163, 168}), HuffmanNode(-1, 162, {164, 165}), HuffmanNode(124, 163, {-1, -1}),
        HuffmanNode(-1, 163, {166, 167}), HuffmanNode(35, 165, {-1, -1}),   HuffmanNode(62, 165, {-1, -1}),
        HuffmanNode(-1, 162, {169, 176}), HuffmanNode(-1, 168, {170, 173}), HuffmanNode(-1, 169, {171, 172}),
        HuffmanNode(0, 170, {-1, -1}),    HuffmanNode(36, 170, {-1, -1}),   HuffmanNode(-1, 169, {174, 175}),
        HuffmanNode(64, 173, {-1, -1}),   HuffmanNode(91, 173, {-1, -1}),   HuffmanNode(-1, 168, {177, 180}),
        HuffmanNode(-1, 176, {178, 179}), HuffmanNode(93, 177, {-1, -1}),   HuffmanNode(126, 177, {-1, -1}),
        HuffmanNode(-1, 176, {181, 184}), HuffmanNode(-1, 180, {182, 183}), HuffmanNode(94, 181, {-1, -1}),
        HuffmanNode(125, 181, {-1, -1}),  HuffmanNode(-1, 180, {185, 188}), HuffmanNode(-1, 184, {186, 187}),
        HuffmanNode(60, 185, {-1, -1}),   HuffmanNode(96, 185, {-1, -1}),   HuffmanNode(-1, 184, {189, 190}),
        HuffmanNode(123, 188, {-1, -1}),  HuffmanNode(-1, 188, {191, 220}), HuffmanNode(-1, 190, {192, 201}),
        HuffmanNode(-1, 191, {193, 196}), HuffmanNode(-1, 192, {194, 195}), HuffmanNode(92, 193, {-1, -1}),
        HuffmanNode(195, 193, {-1, -1}),  HuffmanNode(-1, 192, {197, 198}), HuffmanNode(208, 196, {-1, -1}),
        HuffmanNode(-1, 196, {199, 200}), HuffmanNode(128, 198, {-1, -1}),  HuffmanNode(130, 198, {-1, -1}),
        HuffmanNode(-1, 191, {202, 209}), HuffmanNode(-1, 201, {203, 206}), HuffmanNode(-1, 202, {204, 205}),
        HuffmanNode(131, 203, {-1, -1}),  HuffmanNode(162, 203, {-1, -1}),  HuffmanNode(-1, 202, {207, 208}),
        HuffmanNode(184, 206, {-1, -1}),  HuffmanNode(194, 206, {-1, -1}),  HuffmanNode(-1, 201, {210, 213}),
        HuffmanNode(-1, 209, {211, 212}), HuffmanNode(224, 210, {-1, -1}),  HuffmanNode(226, 210, {-1, -1}),
        HuffmanNode(-1, 209, {214, 217}), HuffmanNode(-1, 213, {215, 216}), HuffmanNode(153, 214, {-1, -1}),
        HuffmanNode(161, 214, {-1, -1}),  HuffmanNode(-1, 213, {218, 219}), HuffmanNode(167, 217, {-1, -1}),
        HuffmanNode(172, 217, {-1, -1}),  HuffmanNode(-1, 190, {221, 266}), HuffmanNode(-1, 220, {222, 237}),
        HuffmanNode(-1, 221, {223, 230}), HuffmanNode(-1, 222, {224, 227}), HuffmanNode(-1, 223, {225, 226}),
        HuffmanNode(176, 224, {-1, -1}),  HuffmanNode(177, 224, {-1, -1}),  HuffmanNode(-1, 223, {228, 229}),
        HuffmanNode(179, 227, {-1, -1}),  HuffmanNode(209, 227, {-1, -1}),  HuffmanNode(-1, 222, {231, 234}),
        HuffmanNode(-1, 230, {232, 233}), HuffmanNode(216, 231, {-1, -1}),  HuffmanNode(217, 231, {-1, -1}),
        HuffmanNode(-1, 230, {235, 236}), HuffmanNode(227, 234, {-1, -1}),  HuffmanNode(229, 234, {-1, -1}),
        HuffmanNode(-1, 221, {238, 251}), HuffmanNode(-1, 237, {239, 244}), HuffmanNode(-1, 238, {240, 241}),
        HuffmanNode(230, 239, {-1, -1}),  HuffmanNode(-1, 239, {242, 243}), HuffmanNode(129, 241, {-1, -1}),
        HuffmanNode(132, 241, {-1, -1}),  HuffmanNode(-1, 238, {245, 248}), HuffmanNode(-1, 244, {246, 247}),
        HuffmanNode(133, 245, {-1, -1}),  HuffmanNode(134, 245, {-1, -1}),  HuffmanNode(-1, 244, {249, 250}),
        HuffmanNode(136, 248, {-1, -1}),  HuffmanNode(146, 248, {-1, -1}),  HuffmanNode(-1, 237, {252, 259}),
        HuffmanNode(-1, 251, {253, 256}), HuffmanNode(-1, 252, {254, 255}), HuffmanNode(154, 253, {-1, -1}),
        HuffmanNode(156, 253, {-1, -1}),  HuffmanNode(-1, 252, {257, 258}), HuffmanNode(160, 256, {-1, -1}),
        HuffmanNode(163, 256, {-1, -1}),  HuffmanNode(-1, 251, {260, 263}), HuffmanNode(-1, 259, {261, 262}),
        HuffmanNode(164, 260, {-1, -1}),  HuffmanNode(169, 260, {-1, -1}),  HuffmanNode(-1, 259, {264, 265}),
        HuffmanNode(170, 263, {-1, -1}),  HuffmanNode(173, 263, {-1, -1}),  HuffmanNode(-1, 220, {267, 306}),
        HuffmanNode(-1, 266, {268, 283}), HuffmanNode(-1, 267, {269, 276}), HuffmanNode(-1, 268, {270, 273}),
        HuffmanNode(-1, 269, {271, 272}), HuffmanNode(178, 270, {-1, -1}),  HuffmanNode(181, 270, {-1, -1}),
        HuffmanNode(-1, 269, {274, 275}), HuffmanNode(185, 273, {-1, -1}),  HuffmanNode(186, 273, {-1, -1}),
        HuffmanNode(-1, 268, {277, 280}), HuffmanNode(-1, 276, {278, 279}), HuffmanNode(187, 277, {-1, -1}),
        HuffmanNode(189, 277, {-1, -1}),  HuffmanNode(-1, 276, {281, 282}), HuffmanNode(190, 280, {-1, -1}),
        HuffmanNode(196, 280, {-1, -1}),  HuffmanNode(-1, 267, {284, 291}), HuffmanNode(-1, 283, {285, 288}),
        HuffmanNode(-1, 284, {286, 287}), HuffmanNode(198, 285, {-1, -1}),  HuffmanNode(228, 285, {-1, -1}),
        HuffmanNode(-1, 284, {289, 290}), HuffmanNode(232, 288, {-1, -1}),  HuffmanNode(233, 288, {-1, -1}),
        HuffmanNode(-1, 283, {292, 299}), HuffmanNode(-1, 291, {293, 296}), HuffmanNode(-1, 292, {294, 295}),
        HuffmanNode(1, 293, {-1, -1}),    HuffmanNode(135, 293, {-1, -1}),  HuffmanNode(-1, 292, {297, 298}),
        HuffmanNode(137, 296, {-1, -1}),  HuffmanNode(138, 296, {-1, -1}),  HuffmanNode(-1, 291, {300, 303}),
        HuffmanNode(-1, 299, {301, 302}), HuffmanNode(139, 300, {-1, -1}),  HuffmanNode(140, 300, {-1, -1}),
        HuffmanNode(-1, 299, {304, 305}), HuffmanNode(141, 303, {-1, -1}),  HuffmanNode(143, 303, {-1, -1}),
        HuffmanNode(-1, 266, {307, 338}), HuffmanNode(-1, 306, {308, 323}), HuffmanNode(-1, 307, {309, 316}),
        HuffmanNode(-1, 308, {310, 313}), HuffmanNode(-1, 309, {311, 312}), HuffmanNode(147, 310, {-1, -1}),
        HuffmanNode(149, 310, {-1, -1}),  HuffmanNode(-1, 309, {314, 315}), HuffmanNode(150, 313, {-1, -1}),
        HuffmanNode(151, 313, {-1, -1}),  HuffmanNode(-1, 308, {317, 320}), HuffmanNode(-1, 316, {318, 319}),
        HuffmanNode(152, 317, {-1, -1}),  HuffmanNode(155, 317, {-1, -1}),  HuffmanNode(-1, 316, {321, 322}),
        HuffmanNode(157, 320, {-1, -1}),  HuffmanNode(158, 320, {-1, -1}),  HuffmanNode(-1, 307, {324, 331}),
        HuffmanNode(-1, 323, {325, 328}), HuffmanNode(-1, 324, {326, 327}), HuffmanNode(165, 325, {-1, -1}),
        HuffmanNode(166, 325, {-1, -1}),  HuffmanNode(-1, 324, {329, 330}), HuffmanNode(168, 328, {-1, -1}),
        HuffmanNode(174, 328, {-1, -1}),  HuffmanNode(-1, 323, {332, 335}), HuffmanNode(-1, 331, {333, 334}),
        HuffmanNode(175, 332, {-1, -1}),  HuffmanNode(180, 332, {-1, -1}),  HuffmanNode(-1, 331, {336, 337}),
        HuffmanNode(182, 335, {-1, -1}),  HuffmanNode(183, 335, {-1, -1}),  HuffmanNode(-1, 306, {339, 360}),
        HuffmanNode(-1, 338, {340, 347}), HuffmanNode(-1, 339, {341, 344}), HuffmanNode(-1, 340, {342, 343}),
        HuffmanNode(188, 341, {-1, -1}),  HuffmanNode(191, 341, {-1, -1}),  HuffmanNode(-1, 340, {345, 346}),
        HuffmanNode(197, 344, {-1, -1}),  HuffmanNode(231, 344, {-1, -1}),  HuffmanNode(-1, 339, {348, 353}),
        HuffmanNode(-1, 347, {349, 350}), HuffmanNode(239, 348, {-1, -1}),  HuffmanNode(-1, 348, {351, 352}),
        HuffmanNode(9, 350, {-1, -1}),    HuffmanNode(142, 350, {-1, -1}),  HuffmanNode(-1, 347, {354, 357}),
        HuffmanNode(-1, 353, {355, 356}), HuffmanNode(144, 354, {-1, -1}),  HuffmanNode(145, 354, {-1, -1}),
        HuffmanNode(-1, 353, {358, 359}), HuffmanNode(148, 357, {-1, -1}),  HuffmanNode(159, 357, {-1, -1}),
        HuffmanNode(-1, 338, {361, 380}), HuffmanNode(-1, 360, {362, 369}), HuffmanNode(-1, 361, {363, 366}),
        HuffmanNode(-1, 362, {364, 365}), HuffmanNode(171, 363, {-1, -1}),  HuffmanNode(206, 363, {-1, -1}),
        HuffmanNode(-1, 362, {367, 368}), HuffmanNode(215, 366, {-1, -1}),  HuffmanNode(225, 366, {-1, -1}),
        HuffmanNode(-1, 361, {370, 373}), HuffmanNode(-1, 369, {371, 372}), HuffmanNode(236, 370, {-1, -1}),
        HuffmanNode(237, 370, {-1, -1}),  HuffmanNode(-1, 369, {374, 377}), HuffmanNode(-1, 373, {375, 376}),
        HuffmanNode(199, 374, {-1, -1}),  HuffmanNode(207, 374, {-1, -1}),  HuffmanNode(-1, 373, {378, 379}),
        HuffmanNode(234, 377, {-1, -1}),  HuffmanNode(235, 377, {-1, -1}),  HuffmanNode(-1, 360, {381, 414}),
        HuffmanNode(-1, 380, {382, 397}), HuffmanNode(-1, 381, {383, 390}), HuffmanNode(-1, 382, {384, 387}),
        HuffmanNode(-1, 383, {385, 386}), HuffmanNode(192, 384, {-1, -1}),  HuffmanNode(193, 384, {-1, -1}),
        HuffmanNode(-1, 383, {388, 389}), HuffmanNode(200, 387, {-1, -1}),  HuffmanNode(201, 387, {-1, -1}),
        HuffmanNode(-1, 382, {391, 394}), HuffmanNode(-1, 390, {392, 393}), HuffmanNode(202, 391, {-1, -1}),
        HuffmanNode(205, 391, {-1, -1}),  HuffmanNode(-1, 390, {395, 396}), HuffmanNode(210, 394, {-1, -1}),
        HuffmanNode(213, 394, {-1, -1}),  HuffmanNode(-1, 381, {398, 405}), HuffmanNode(-1, 397, {399, 402}),
        HuffmanNode(-1, 398, {400, 401}), HuffmanNode(218, 399, {-1, -1}),  HuffmanNode(219, 399, {-1, -1}),
        HuffmanNode(-1, 398, {403, 404}), HuffmanNode(238, 402, {-1, -1}),  HuffmanNode(240, 402, {-1, -1}),
        HuffmanNode(-1, 397, {406, 409}), HuffmanNode(-1, 405, {407, 408}), HuffmanNode(242, 406, {-1, -1}),
        HuffmanNode(243, 406, {-1, -1}),  HuffmanNode(-1, 405, {410, 411}), HuffmanNode(255, 409, {-1, -1}),
        HuffmanNode(-1, 409, {412, 413}), HuffmanNode(203, 411, {-1, -1}),  HuffmanNode(204, 411, {-1, -1}),
        HuffmanNode(-1, 380, {415, 446}), HuffmanNode(-1, 414, {416, 431}), HuffmanNode(-1, 415, {417, 424}),
        HuffmanNode(-1, 416, {418, 421}), HuffmanNode(-1, 417, {419, 420}), HuffmanNode(211, 418, {-1, -1}),
        HuffmanNode(212, 418, {-1, -1}),  HuffmanNode(-1, 417, {422, 423}), HuffmanNode(214, 421, {-1, -1}),
        HuffmanNode(221, 421, {-1, -1}),  HuffmanNode(-1, 416, {425, 428}), HuffmanNode(-1, 424, {426, 427}),
        HuffmanNode(222, 425, {-1, -1}),  HuffmanNode(223, 425, {-1, -1}),  HuffmanNode(-1, 424, {429, 430}),
        HuffmanNode(241, 428, {-1, -1}),  HuffmanNode(244, 428, {-1, -1}),  HuffmanNode(-1, 415, {432, 439}),
        HuffmanNode(-1, 431, {433, 436}), HuffmanNode(-1, 432, {434, 435}), HuffmanNode(245, 433, {-1, -1}),
        HuffmanNode(246, 433, {-1, -1}),  HuffmanNode(-1, 432, {437, 438}), HuffmanNode(247, 436, {-1, -1}),
        HuffmanNode(248, 436, {-1, -1}),  HuffmanNode(-1, 431, {440, 443}), HuffmanNode(-1, 439, {441, 442}),
        HuffmanNode(250, 440, {-1, -1}),  HuffmanNode(251, 440, {-1, -1}),  HuffmanNode(-1, 439, {444, 445}),
        HuffmanNode(252, 443, {-1, -1}),  HuffmanNode(253, 443, {-1, -1}),  HuffmanNode(-1, 414, {447, 476}),
        HuffmanNode(-1, 446, {448, 461}), HuffmanNode(-1, 447, {449, 454}), HuffmanNode(-1, 448, {450, 451}),
        HuffmanNode(254, 449, {-1, -1}),  HuffmanNode(-1, 449, {452, 453}), HuffmanNode(2, 451, {-1, -1}),
        HuffmanNode(3, 451, {-1, -1}),    HuffmanNode(-1, 448, {455, 458}), HuffmanNode(-1, 454, {456, 457}),
        HuffmanNode(4, 455, {-1, -1}),    HuffmanNode(5, 455, {-1, -1}),    HuffmanNode(-1, 454, {459, 460}),
        HuffmanNode(6, 458, {-1, -1}),    HuffmanNode(7, 458, {-1, -1}),    HuffmanNode(-1, 447, {462, 469}),
        HuffmanNode(-1, 461, {463, 466}), HuffmanNode(-1, 462, {464, 465}), HuffmanNode(8, 463, {-1, -1}),
        HuffmanNode(11, 463, {-1, -1}),   HuffmanNode(-1, 462, {467, 468}), HuffmanNode(12, 466, {-1, -1}),
        HuffmanNode(14, 466, {-1, -1}),   HuffmanNode(-1, 461, {470, 473}), HuffmanNode(-1, 469, {471, 472}),
        HuffmanNode(15, 470, {-1, -1}),   HuffmanNode(16, 470, {-1, -1}),   HuffmanNode(-1, 469, {474, 475}),
        HuffmanNode(17, 473, {-1, -1}),   HuffmanNode(18, 473, {-1, -1}),   HuffmanNode(-1, 446, {477, 492}),
        HuffmanNode(-1, 476, {478, 485}), HuffmanNode(-1, 477, {479, 482}), HuffmanNode(-1, 478, {480, 481}),
        HuffmanNode(19, 479, {-1, -1}),   HuffmanNode(20, 479, {-1, -1}),   HuffmanNode(-1, 478, {483, 484}),
        HuffmanNode(21, 482, {-1, -1}),   HuffmanNode(23, 482, {-1, -1}),   HuffmanNode(-1, 477, {486, 489}),
        HuffmanNode(-1, 485, {487, 488}), HuffmanNode(24, 486, {-1, -1}),   HuffmanNode(25, 486, {-1, -1}),
        HuffmanNode(-1, 485, {490, 491}), HuffmanNode(26, 489, {-1, -1}),   HuffmanNode(27, 489, {-1, -1}),
        HuffmanNode(-1, 476, {493, 500}), HuffmanNode(-1, 492, {494, 497}), HuffmanNode(-1, 493, {495, 496}),
        HuffmanNode(28, 494, {-1, -1}),   HuffmanNode(29, 494, {-1, -1}),   HuffmanNode(-1, 493, {498, 499}),
        HuffmanNode(30, 497, {-1, -1}),   HuffmanNode(31, 497, {-1, -1}),   HuffmanNode(-1, 492, {501, 504}),
        HuffmanNode(-1, 500, {502, 503}), HuffmanNode(127, 501, {-1, -1}),  HuffmanNode(220, 501, {-1, -1}),
        HuffmanNode(-1, 500, {505, 506}), HuffmanNode(249, 504, {-1, -1}),  HuffmanNode(-1, 504, {507, 510}),
        HuffmanNode(-1, 506, {508, 509}), HuffmanNode(10, 507, {-1, -1}),   HuffmanNode(13, 507, {-1, -1}),
        HuffmanNode(-1, 506, {511, -1}),  HuffmanNode(22, 510, {-1, -1})};
};

} // namespace http2::detail
ILIAS_NS_END