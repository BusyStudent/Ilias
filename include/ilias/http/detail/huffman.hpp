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
        uint8_t remainBits = huffmanCode.encodeBits - copyBytes * 8; // this huffmanCode remain bits after copy bytes as much as possible.
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
                // then this byte will remain 8 - remainBits - bitsOffset bits, the offset is 8 - remain bits. and it must less then 8.
                return remainBits + bitsOffset >= 8 ? 0 : remainBits + bitsOffset;
            }
            else {
                outputBuffer.push_back(std::byte(hc)); // write bits in next byte.
                return remainBits; // then this byte will remain 8 - remainBits.
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
    inline static int decode(std::span<const std::byte> buffer, std::vector<std::byte> &outputBuffer) {
        ILIAS_ASSERT(buffer.size() > 0);
        uint8_t bitOffset = 0;

        int ByteOffset = 0, code = 0;
        while (ByteOffset < buffer.size()) {
            auto offset = decodeOne(buffer, ByteOffset, bitOffset, code);
            if (offset == -1) {
                break;
            }
            if (code == HTTP_2_HPACK_HUFFMAN_CODE_EOS) {
                return -1;
            }
            ByteOffset = offset;
            outputBuffer.push_back(std::byte(code));
        }
        if (bitOffset != 0) {
            ByteOffset += 1;
        }
        return ByteOffset;
    }
    inline static int decodeOne(std::span<const std::byte> buffer, const std::size_t byteOffset, uint8_t &bitOffset,
                                int &decode_code) {
        ILIAS_ASSERT(byteOffset < buffer.size());
        ILIAS_ASSERT(bitOffset < 8);

        uint32_t code = 0, bits = 0;
        auto     start = kStaticHuffmanCodeSortByHex.begin();
        while (bits < 30 && (bits < 5 || start->encode != code || start->encodeBits != bits)) {
            if (bits + bitOffset < 8) {
                code = (code << 1) | ((static_cast<unsigned char>(buffer[byteOffset]) >> (7 - bits - bitOffset)) & 1);
            }
            else if (byteOffset + (bits + bitOffset) / 8 < buffer.size()) {
                code = (code << 1) | ((static_cast<unsigned char>(buffer[byteOffset + (bits + bitOffset) / 8]) >>
                                       (7 - (bits + bitOffset) % 8)) &
                                      1);
            }
            else {
                return -1;
            }
            auto f =
                std::lower_bound(start, kStaticHuffmanCodeSortByHex.end(), HuffmanCode {0, code, bits},
                                 [](const HuffmanCode &a1, const HuffmanCode &a2) { return a1.encode < a2.encode; });
            if (f == kStaticHuffmanCodeSortByHex.end()) {
                return -1;
            }
            start = f;
            ++bits;
        }
        decode_code      = start->rawCode;
        auto byteOffset_ = byteOffset + (bits + bitOffset) / 8;
        bitOffset        = (bits + bitOffset) % 8;
        return byteOffset_;
    }

private:
    // TODO: maybe it would be better to use binary tree here
    static constexpr const std::array<HuffmanCode, 257> kStaticHuffmanCodeSortByHex = {
        HuffmanCode {48, 0x0, 5},         HuffmanCode {49, 0x1, 5},         HuffmanCode {50, 0x2, 5},
        HuffmanCode {97, 0x3, 5},         HuffmanCode {99, 0x4, 5},         HuffmanCode {101, 0x5, 5},
        HuffmanCode {105, 0x6, 5},        HuffmanCode {111, 0x7, 5},        HuffmanCode {115, 0x8, 5},
        HuffmanCode {116, 0x9, 5},        HuffmanCode {32, 0x14, 6},        HuffmanCode {37, 0x15, 6},
        HuffmanCode {45, 0x16, 6},        HuffmanCode {46, 0x17, 6},        HuffmanCode {47, 0x18, 6},
        HuffmanCode {51, 0x19, 6},        HuffmanCode {52, 0x1a, 6},        HuffmanCode {53, 0x1b, 6},
        HuffmanCode {54, 0x1c, 6},        HuffmanCode {55, 0x1d, 6},        HuffmanCode {56, 0x1e, 6},
        HuffmanCode {57, 0x1f, 6},        HuffmanCode {61, 0x20, 6},        HuffmanCode {65, 0x21, 6},
        HuffmanCode {95, 0x22, 6},        HuffmanCode {98, 0x23, 6},        HuffmanCode {100, 0x24, 6},
        HuffmanCode {102, 0x25, 6},       HuffmanCode {103, 0x26, 6},       HuffmanCode {104, 0x27, 6},
        HuffmanCode {108, 0x28, 6},       HuffmanCode {109, 0x29, 6},       HuffmanCode {110, 0x2a, 6},
        HuffmanCode {112, 0x2b, 6},       HuffmanCode {114, 0x2c, 6},       HuffmanCode {117, 0x2d, 6},
        HuffmanCode {58, 0x5c, 7},        HuffmanCode {66, 0x5d, 7},        HuffmanCode {67, 0x5e, 7},
        HuffmanCode {68, 0x5f, 7},        HuffmanCode {69, 0x60, 7},        HuffmanCode {70, 0x61, 7},
        HuffmanCode {71, 0x62, 7},        HuffmanCode {72, 0x63, 7},        HuffmanCode {73, 0x64, 7},
        HuffmanCode {74, 0x65, 7},        HuffmanCode {75, 0x66, 7},        HuffmanCode {76, 0x67, 7},
        HuffmanCode {77, 0x68, 7},        HuffmanCode {78, 0x69, 7},        HuffmanCode {79, 0x6a, 7},
        HuffmanCode {80, 0x6b, 7},        HuffmanCode {81, 0x6c, 7},        HuffmanCode {82, 0x6d, 7},
        HuffmanCode {83, 0x6e, 7},        HuffmanCode {84, 0x6f, 7},        HuffmanCode {85, 0x70, 7},
        HuffmanCode {86, 0x71, 7},        HuffmanCode {87, 0x72, 7},        HuffmanCode {89, 0x73, 7},
        HuffmanCode {106, 0x74, 7},       HuffmanCode {107, 0x75, 7},       HuffmanCode {113, 0x76, 7},
        HuffmanCode {118, 0x77, 7},       HuffmanCode {119, 0x78, 7},       HuffmanCode {120, 0x79, 7},
        HuffmanCode {121, 0x7a, 7},       HuffmanCode {122, 0x7b, 7},       HuffmanCode {38, 0xf8, 8},
        HuffmanCode {42, 0xf9, 8},        HuffmanCode {44, 0xfa, 8},        HuffmanCode {59, 0xfb, 8},
        HuffmanCode {88, 0xfc, 8},        HuffmanCode {90, 0xfd, 8},        HuffmanCode {33, 0x3f8, 10},
        HuffmanCode {34, 0x3f9, 10},      HuffmanCode {40, 0x3fa, 10},      HuffmanCode {41, 0x3fb, 10},
        HuffmanCode {63, 0x3fc, 10},      HuffmanCode {39, 0x7fa, 11},      HuffmanCode {43, 0x7fb, 11},
        HuffmanCode {124, 0x7fc, 11},     HuffmanCode {35, 0xffa, 12},      HuffmanCode {62, 0xffb, 12},
        HuffmanCode {0, 0x1ff8, 13},      HuffmanCode {36, 0x1ff9, 13},     HuffmanCode {64, 0x1ffa, 13},
        HuffmanCode {91, 0x1ffb, 13},     HuffmanCode {93, 0x1ffc, 13},     HuffmanCode {126, 0x1ffd, 13},
        HuffmanCode {94, 0x3ffc, 14},     HuffmanCode {125, 0x3ffd, 14},    HuffmanCode {60, 0x7ffc, 15},
        HuffmanCode {96, 0x7ffd, 15},     HuffmanCode {123, 0x7ffe, 15},    HuffmanCode {92, 0x7fff0, 19},
        HuffmanCode {195, 0x7fff1, 19},   HuffmanCode {208, 0x7fff2, 19},   HuffmanCode {128, 0xfffe6, 20},
        HuffmanCode {130, 0xfffe7, 20},   HuffmanCode {131, 0xfffe8, 20},   HuffmanCode {162, 0xfffe9, 20},
        HuffmanCode {184, 0xfffea, 20},   HuffmanCode {194, 0xfffeb, 20},   HuffmanCode {224, 0xfffec, 20},
        HuffmanCode {226, 0xfffed, 20},   HuffmanCode {153, 0x1fffdc, 21},  HuffmanCode {161, 0x1fffdd, 21},
        HuffmanCode {167, 0x1fffde, 21},  HuffmanCode {172, 0x1fffdf, 21},  HuffmanCode {176, 0x1fffe0, 21},
        HuffmanCode {177, 0x1fffe1, 21},  HuffmanCode {179, 0x1fffe2, 21},  HuffmanCode {209, 0x1fffe3, 21},
        HuffmanCode {216, 0x1fffe4, 21},  HuffmanCode {217, 0x1fffe5, 21},  HuffmanCode {227, 0x1fffe6, 21},
        HuffmanCode {229, 0x1fffe7, 21},  HuffmanCode {230, 0x1fffe8, 21},  HuffmanCode {129, 0x3fffd2, 22},
        HuffmanCode {132, 0x3fffd3, 22},  HuffmanCode {133, 0x3fffd4, 22},  HuffmanCode {134, 0x3fffd5, 22},
        HuffmanCode {136, 0x3fffd6, 22},  HuffmanCode {146, 0x3fffd7, 22},  HuffmanCode {154, 0x3fffd8, 22},
        HuffmanCode {156, 0x3fffd9, 22},  HuffmanCode {160, 0x3fffda, 22},  HuffmanCode {163, 0x3fffdb, 22},
        HuffmanCode {164, 0x3fffdc, 22},  HuffmanCode {169, 0x3fffdd, 22},  HuffmanCode {170, 0x3fffde, 22},
        HuffmanCode {173, 0x3fffdf, 22},  HuffmanCode {178, 0x3fffe0, 22},  HuffmanCode {181, 0x3fffe1, 22},
        HuffmanCode {185, 0x3fffe2, 22},  HuffmanCode {186, 0x3fffe3, 22},  HuffmanCode {187, 0x3fffe4, 22},
        HuffmanCode {189, 0x3fffe5, 22},  HuffmanCode {190, 0x3fffe6, 22},  HuffmanCode {196, 0x3fffe7, 22},
        HuffmanCode {198, 0x3fffe8, 22},  HuffmanCode {228, 0x3fffe9, 22},  HuffmanCode {232, 0x3fffea, 22},
        HuffmanCode {233, 0x3fffeb, 22},  HuffmanCode {1, 0x7fffd8, 23},    HuffmanCode {135, 0x7fffd9, 23},
        HuffmanCode {137, 0x7fffda, 23},  HuffmanCode {138, 0x7fffdb, 23},  HuffmanCode {139, 0x7fffdc, 23},
        HuffmanCode {140, 0x7fffdd, 23},  HuffmanCode {141, 0x7fffde, 23},  HuffmanCode {143, 0x7fffdf, 23},
        HuffmanCode {147, 0x7fffe0, 23},  HuffmanCode {149, 0x7fffe1, 23},  HuffmanCode {150, 0x7fffe2, 23},
        HuffmanCode {151, 0x7fffe3, 23},  HuffmanCode {152, 0x7fffe4, 23},  HuffmanCode {155, 0x7fffe5, 23},
        HuffmanCode {157, 0x7fffe6, 23},  HuffmanCode {158, 0x7fffe7, 23},  HuffmanCode {165, 0x7fffe8, 23},
        HuffmanCode {166, 0x7fffe9, 23},  HuffmanCode {168, 0x7fffea, 23},  HuffmanCode {174, 0x7fffeb, 23},
        HuffmanCode {175, 0x7fffec, 23},  HuffmanCode {180, 0x7fffed, 23},  HuffmanCode {182, 0x7fffee, 23},
        HuffmanCode {183, 0x7fffef, 23},  HuffmanCode {188, 0x7ffff0, 23},  HuffmanCode {191, 0x7ffff1, 23},
        HuffmanCode {197, 0x7ffff2, 23},  HuffmanCode {231, 0x7ffff3, 23},  HuffmanCode {239, 0x7ffff4, 23},
        HuffmanCode {9, 0xffffea, 24},    HuffmanCode {142, 0xffffeb, 24},  HuffmanCode {144, 0xffffec, 24},
        HuffmanCode {145, 0xffffed, 24},  HuffmanCode {148, 0xffffee, 24},  HuffmanCode {159, 0xffffef, 24},
        HuffmanCode {171, 0xfffff0, 24},  HuffmanCode {206, 0xfffff1, 24},  HuffmanCode {215, 0xfffff2, 24},
        HuffmanCode {225, 0xfffff3, 24},  HuffmanCode {236, 0xfffff4, 24},  HuffmanCode {237, 0xfffff5, 24},
        HuffmanCode {199, 0x1ffffec, 25}, HuffmanCode {207, 0x1ffffed, 25}, HuffmanCode {234, 0x1ffffee, 25},
        HuffmanCode {235, 0x1ffffef, 25}, HuffmanCode {192, 0x3ffffe0, 26}, HuffmanCode {193, 0x3ffffe1, 26},
        HuffmanCode {200, 0x3ffffe2, 26}, HuffmanCode {201, 0x3ffffe3, 26}, HuffmanCode {202, 0x3ffffe4, 26},
        HuffmanCode {205, 0x3ffffe5, 26}, HuffmanCode {210, 0x3ffffe6, 26}, HuffmanCode {213, 0x3ffffe7, 26},
        HuffmanCode {218, 0x3ffffe8, 26}, HuffmanCode {219, 0x3ffffe9, 26}, HuffmanCode {238, 0x3ffffea, 26},
        HuffmanCode {240, 0x3ffffeb, 26}, HuffmanCode {242, 0x3ffffec, 26}, HuffmanCode {243, 0x3ffffed, 26},
        HuffmanCode {255, 0x3ffffee, 26}, HuffmanCode {203, 0x7ffffde, 27}, HuffmanCode {204, 0x7ffffdf, 27},
        HuffmanCode {211, 0x7ffffe0, 27}, HuffmanCode {212, 0x7ffffe1, 27}, HuffmanCode {214, 0x7ffffe2, 27},
        HuffmanCode {221, 0x7ffffe3, 27}, HuffmanCode {222, 0x7ffffe4, 27}, HuffmanCode {223, 0x7ffffe5, 27},
        HuffmanCode {241, 0x7ffffe6, 27}, HuffmanCode {244, 0x7ffffe7, 27}, HuffmanCode {245, 0x7ffffe8, 27},
        HuffmanCode {246, 0x7ffffe9, 27}, HuffmanCode {247, 0x7ffffea, 27}, HuffmanCode {248, 0x7ffffeb, 27},
        HuffmanCode {250, 0x7ffffec, 27}, HuffmanCode {251, 0x7ffffed, 27}, HuffmanCode {252, 0x7ffffee, 27},
        HuffmanCode {253, 0x7ffffef, 27}, HuffmanCode {254, 0x7fffff0, 27}, HuffmanCode {2, 0xfffffe2, 28},
        HuffmanCode {3, 0xfffffe3, 28},   HuffmanCode {4, 0xfffffe4, 28},   HuffmanCode {5, 0xfffffe5, 28},
        HuffmanCode {6, 0xfffffe6, 28},   HuffmanCode {7, 0xfffffe7, 28},   HuffmanCode {8, 0xfffffe8, 28},
        HuffmanCode {11, 0xfffffe9, 28},  HuffmanCode {12, 0xfffffea, 28},  HuffmanCode {14, 0xfffffeb, 28},
        HuffmanCode {15, 0xfffffec, 28},  HuffmanCode {16, 0xfffffed, 28},  HuffmanCode {17, 0xfffffee, 28},
        HuffmanCode {18, 0xfffffef, 28},  HuffmanCode {19, 0xffffff0, 28},  HuffmanCode {20, 0xffffff1, 28},
        HuffmanCode {21, 0xffffff2, 28},  HuffmanCode {23, 0xffffff3, 28},  HuffmanCode {24, 0xffffff4, 28},
        HuffmanCode {25, 0xffffff5, 28},  HuffmanCode {26, 0xffffff6, 28},  HuffmanCode {27, 0xffffff7, 28},
        HuffmanCode {28, 0xffffff8, 28},  HuffmanCode {29, 0xffffff9, 28},  HuffmanCode {30, 0xffffffa, 28},
        HuffmanCode {31, 0xffffffb, 28},  HuffmanCode {127, 0xffffffc, 28}, HuffmanCode {220, 0xffffffd, 28},
        HuffmanCode {249, 0xffffffe, 28}, HuffmanCode {10, 0x3ffffffc, 30}, HuffmanCode {13, 0x3ffffffd, 30},
        HuffmanCode {22, 0x3ffffffe, 30}, HuffmanCode {256, 0x3fffffff, 30}};
};

} // namespace http2::detail
ILIAS_NS_END