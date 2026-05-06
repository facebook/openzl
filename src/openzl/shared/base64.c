// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/shared/base64.h"

static const char kBase64Map[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

/* Decode table: valid base64 chars map to their 6-bit value + 1.
 * Invalid chars map to 0, which lets us detect them during decoding. */
static const uint8_t kBase64DecodeMap[256] = {
    ['A'] = 1,  ['B'] = 2,  ['C'] = 3,  ['D'] = 4,  ['E'] = 5,  ['F'] = 6,
    ['G'] = 7,  ['H'] = 8,  ['I'] = 9,  ['J'] = 10, ['K'] = 11, ['L'] = 12,
    ['M'] = 13, ['N'] = 14, ['O'] = 15, ['P'] = 16, ['Q'] = 17, ['R'] = 18,
    ['S'] = 19, ['T'] = 20, ['U'] = 21, ['V'] = 22, ['W'] = 23, ['X'] = 24,
    ['Y'] = 25, ['Z'] = 26, ['a'] = 27, ['b'] = 28, ['c'] = 29, ['d'] = 30,
    ['e'] = 31, ['f'] = 32, ['g'] = 33, ['h'] = 34, ['i'] = 35, ['j'] = 36,
    ['k'] = 37, ['l'] = 38, ['m'] = 39, ['n'] = 40, ['o'] = 41, ['p'] = 42,
    ['q'] = 43, ['r'] = 44, ['s'] = 45, ['t'] = 46, ['u'] = 47, ['v'] = 48,
    ['w'] = 49, ['x'] = 50, ['y'] = 51, ['z'] = 52, ['0'] = 53, ['1'] = 54,
    ['2'] = 55, ['3'] = 56, ['4'] = 57, ['5'] = 58, ['6'] = 59, ['7'] = 60,
    ['8'] = 61, ['9'] = 62, ['+'] = 63, ['/'] = 64,
};

size_t ZL_base64EncodedSize(size_t srcSize)
{
    size_t encodedSize = (srcSize / 3) * 4;
    if (srcSize % 3 != 0) {
        encodedSize += 4;
    }
    return encodedSize;
}

ZL_Report
ZL_base64Encode(char* dst, size_t dstCap, const uint8_t* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (srcSize == 0) {
        return ZL_returnValue(0);
    }
    const size_t encodedSize = ZL_base64EncodedSize(srcSize);
    ZL_ERR_IF(dstCap < encodedSize, dstCapacity_tooSmall);

    char* const dstBegin     = dst;
    const uint8_t* const end = src + srcSize;
    for (; (end - src) >= 3; src += 3, dst += 4) {
        dst[0] = kBase64Map[src[0] >> 2];
        dst[1] = kBase64Map[((src[0] & 0x03) << 4) + (src[1] >> 4)];
        dst[2] = kBase64Map[((src[1] & 0x0f) << 2) + (src[2] >> 6)];
        dst[3] = kBase64Map[src[2] & 0x3f];
    }

    if (src < end) {
        dst[0] = kBase64Map[src[0] >> 2];
        if (src + 1 == end) {
            dst[1] = kBase64Map[(src[0] & 0x03) << 4];
            dst[2] = '=';
        } else {
            dst[1] = kBase64Map[((src[0] & 0x03) << 4) + (src[1] >> 4)];
            dst[2] = kBase64Map[(src[1] & 0x0f) << 2];
        }
        dst[3] = '=';
        dst += 4;
    }

    return ZL_returnValue((size_t)(dst - dstBegin));
}

ZL_Report
ZL_base64Decode(uint8_t* dst, size_t dstCap, const char* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (srcSize == 0) {
        return ZL_returnValue(0);
    }
    ZL_ERR_IF(srcSize % 4 != 0, corruption);

    size_t pad = 0;
    if (src[srcSize - 1] == '=') {
        pad++;
    }
    if (srcSize >= 2 && src[srcSize - 2] == '=') {
        pad++;
    }

    const size_t decodedSize = (srcSize / 4) * 3 - pad;
    ZL_ERR_IF(decodedSize > dstCap, dstCapacity_tooSmall);

    size_t di = 0;
    for (size_t i = 0; i < srcSize; i += 4) {
        const int isLastBlock = (i + 4 == srcSize);
        const uint8_t ra      = kBase64DecodeMap[(uint8_t)src[i]];
        const uint8_t rb      = kBase64DecodeMap[(uint8_t)src[i + 1]];
        const uint8_t rc      = (isLastBlock && src[i + 2] == '=')
                     ? 1
                     : kBase64DecodeMap[(uint8_t)src[i + 2]];
        const uint8_t rd      = (isLastBlock && src[i + 3] == '=')
                     ? 1
                     : kBase64DecodeMap[(uint8_t)src[i + 3]];
        ZL_ERR_IF(ra == 0 || rb == 0 || rc == 0 || rd == 0, corruption);
        const uint8_t a = ra - 1;
        const uint8_t b = rb - 1;
        const uint8_t c = rc - 1;
        const uint8_t d = rd - 1;
        dst[di++]       = (uint8_t)((a << 2) | (b >> 4));
        if (di < decodedSize) {
            dst[di++] = (uint8_t)((b << 4) | (c >> 2));
        }
        if (di < decodedSize) {
            dst[di++] = (uint8_t)((c << 6) | d);
        }
    }

    return ZL_returnValue(decodedSize);
}
