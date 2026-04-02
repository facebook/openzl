// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#ifndef OPENZL_TESTS_DICT_TEST_HELPERS_H
#define OPENZL_TESTS_DICT_TEST_HELPERS_H

#include <cstring>
#include <vector>

#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/fse/common/mem.h"

inline ZL_DictID makeNullDictID()
{
    ZL_DictID id;
    std::memset(&id, 0, sizeof(id));
    return id;
}

inline ZL_DictID makeDictID(uint8_t seed)
{
    ZL_DictID id;
    for (size_t i = 0; i < 4; i++) {
        id.id.bytes[i] = static_cast<uint64_t>(seed + i);
    }
    return id;
}

/// Build a packed dict wire buffer.
inline std::vector<uint8_t> buildPackedDict(
        size_t contentSize,
        ZL_DictID dictID          = makeNullDictID(),
        ZL_IDType codec           = 0,
        uint8_t contentByte       = 0xAB,
        TransformType_e codecType = trt_standard)
{
    std::vector<uint8_t> buf(ZL_DICT_HEADER_SIZE + contentSize, 0);
    uint8_t* p = buf.data();

    MEM_writeLE32(p, ZL_DICT_MAGIC);
    p += 4;

    for (int i = 0; i < 4; i++) {
        MEM_writeLE64(p, dictID.id.bytes[i]);
        p += 8;
    }

    MEM_writeLE32(p, static_cast<uint32_t>(codec));
    p += 4;

    *p = static_cast<uint8_t>(codecType);
    p += 1;

    MEM_writeLE32(p, static_cast<uint32_t>(contentSize));
    p += 4;

    std::memset(p, contentByte, contentSize);
    return buf;
}

/// Build a packed BundleInfo header with N dict IDs.
inline std::vector<uint8_t> buildPackedBundleInfo(
        size_t numDicts,
        uint8_t bundleIDSeed = 0,
        bool isFatBundle     = false)
{
    size_t const totalSize =
            ZL_BUNDLE_HEADER_SIZE + numDicts * ZL_UNIQUE_ID_SIZE;
    std::vector<uint8_t> buf(totalSize, 0);
    uint8_t* p = buf.data();

    MEM_writeLE32(p, ZL_BUNDLEINFO_MAGIC);
    p += 4;

    for (int i = 0; i < 4; i++) {
        MEM_writeLE64(p, static_cast<uint64_t>(bundleIDSeed + i));
        p += 8;
    }

    *p = isFatBundle ? 1 : 0;
    p += 1;

    MEM_writeLE32(p, static_cast<uint32_t>(numDicts));
    p += 4;

    for (size_t i = 0; i < numDicts; i++) {
        for (int j = 0; j < 4; j++) {
            MEM_writeLE64(p, static_cast<uint64_t>((i + 1) * 10 + j));
            p += 8;
        }
    }

    return buf;
}

#endif // OPENZL_TESTS_DICT_TEST_HELPERS_H
