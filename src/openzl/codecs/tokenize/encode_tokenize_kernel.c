// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/tokenize/encode_tokenize_kernel.h"
#include "openzl/codecs/tokenize/encode_tokenize_kernel_sort.h"
#include "openzl/common/allocation.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_errors.h"

ZL_Report ZS_buildTokenizeVsfAlphabet(
        MapVSF* tokToIdx,
        size_t* alphabetFieldSizesSum,
        const uint8_t* src,
        const uint32_t* fieldSizes,
        size_t nbElts)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Reserve map space
    ZL_ERR_IF_NOT(
            MapVSF_reserve(tokToIdx, (uint32_t)ZL_MIN(256, nbElts), false),
            allocation);

    // Build alphabet map
    *alphabetFieldSizesSum = 0;
    const uint8_t* currElt = src;
    bool badAlloc          = false;
    for (size_t i = 0, nextAlphabetIdx = 0; i < nbElts; i++) {
        const VSFKey token = (VSFKey){ currElt, fieldSizes[i] };
        if (!MapVSF_containsVal(tokToIdx, token)) {
            MapVSF_Insert const insert = MapVSF_insertVal(
                    tokToIdx, (MapVSF_Entry){ token, nextAlphabetIdx++ });
            ZL_ASSERT(insert.badAlloc || insert.inserted);
            badAlloc |= insert.badAlloc;
            *alphabetFieldSizesSum += fieldSizes[i];
        }
        currElt += fieldSizes[i];
    }
    ZL_ERR_IF(badAlloc, allocation);
    return ZL_returnSuccess();
}

// Usage: updates each token's index in the map (initially insertion order)
//        to match the order of the keys after sorting (lexical order)
// Example: map = { {"c", 1}: 0, {"a", 1}: 1, {"b", 1}: 2 }
//          keysBuffer = [{"c", 1}, {"a", 1}, {"b", 1}]
//          sort(keysBuffer) = [{"a", 1}, {"b", 1}, {"c", 1}]
//          syncVSFKeyMap(map, keysBuffer) = {"c": 2, "a": 0, "b": 1}
static void syncVSFKeyMap(MapVSF* tokToIdx, const VSFKey* const keysBuffer)
{
    size_t const alphabetSize = MapVSF_size(tokToIdx);
    for (size_t i = 0; i < alphabetSize; i++) {
        MapVSF_Entry* entry = MapVSF_findMutVal(tokToIdx, keysBuffer[i]);
        ZL_ASSERT_NN(entry);
        entry->val = i;
    }
}

static void writeVSFAlphabet(
        const VSFKey* const keysBuffer,
        uint8_t* const alphabetPtr,
        uint32_t* const alphabetFieldSizes,
        size_t const alphabetSize)
{
    uint8_t* nextEltPtr = alphabetPtr;
    for (size_t i = 0; i < alphabetSize; i++) {
        memcpy(nextEltPtr, keysBuffer[i].fieldStart, keysBuffer[i].fieldSize);
        nextEltPtr += keysBuffer[i].fieldSize;
        alphabetFieldSizes[i] = (uint32_t)keysBuffer[i].fieldSize;
    }
}

static void writeVSFIndices(
        MapVSF* tokToIdx,
        const uint8_t* const src,
        const uint32_t* const fieldSizes,
        uint8_t* const indicesPtr,
        size_t const nbElts,
        size_t idxWidth)
{
    const uint8_t* nextEltPtr = src;
    for (size_t i = 0; i < nbElts; i++) {
        VSFKey const token = { nextEltPtr, fieldSizes[i] };
        ZL_ASSERT(MapVSF_containsVal(tokToIdx, token));
        size_t const index = MapVSF_findVal(tokToIdx, token)->val;
        ZL_writeN(indicesPtr + i * idxWidth, index, idxWidth);
        nextEltPtr += fieldSizes[i];
    }
}

ZL_Report ZS_tokenizeVSFEncode(
        uint8_t* const alphabet,
        uint32_t* const alphabetFieldSizes,
        size_t const alphabetSize,
        uint8_t* const indices,
        VSFKey* const keysBuffer,
        const uint8_t* const src,
        const uint32_t* const fieldSizes,
        size_t const nbElts,
        MapVSF* tokToIdx,
        size_t const idxWidth,
        bool sort)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_GT(
            alphabetSize,
            UINT32_MAX,
            temporaryLibraryLimitation,
            "Only 4 byte indices supported... But why do you want this?");
    ZL_ERR_IF_EQ(idxWidth, 0, logicError);

    // Build buffer to get insertion order of map entries
    MapVSF_Iter iter = MapVSF_iter(tokToIdx);
    MapVSF_Entry const* entry;
    for (; (entry = MapVSF_Iter_next(&iter));) {
        memcpy(&keysBuffer[entry->val], entry, sizeof(VSFKey));
    }

    // Sort if needed
    if (sort) {
        pqdsortVsf(keysBuffer, alphabetSize);
        syncVSFKeyMap(tokToIdx, keysBuffer);
    }

    // Write alphabet
    writeVSFAlphabet(keysBuffer, alphabet, alphabetFieldSizes, alphabetSize);

    // Write indices
    writeVSFIndices(tokToIdx, src, fieldSizes, indices, nbElts, idxWidth);

    return ZL_returnSuccess();
}
