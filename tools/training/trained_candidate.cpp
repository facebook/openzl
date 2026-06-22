// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/trained_candidate.h"

#include <cstring>
#include <map>

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_unique_id.h"

namespace openzl::training {

namespace {

std::string hexEncodeUniqueID(const ZL_UniqueID& uid)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(sizeof(uid.bytes) * 2, '\0');
    for (size_t i = 0; i < sizeof(uid.bytes); ++i) {
        result[i * 2]     = kHex[(uid.bytes[i] >> 4) & 0xF];
        result[i * 2 + 1] = kHex[uid.bytes[i] & 0xF];
    }
    return result;
}

/// Re-encode a CBOR item tree to a std::string. Frees the arena on failure.
std::string encodeCBOR(A1C_Item* root, Arena* arena)
{
    size_t encodedSize = A1C_Item_encodedSize(root);
    std::string result(encodedSize, '\0');
    A1C_Error encodeErr{};
    size_t written = A1C_Item_encode(
            root,
            reinterpret_cast<uint8_t*>(result.data()),
            result.size(),
            &encodeErr);
    if (written == 0) {
        ALLOC_Arena_freeArena(arena);
        throw Exception("Failed to re-encode patched compressor CBOR");
    }
    result.resize(written);
    return result;
}

/// Decode a CBOR blob and return the root item. Caller must free the arena.
A1C_Item*
decodeCBOR(const std::string& serialized, Arena* arena, A1C_Arena& a1cArena)
{
    A1C_Decoder decoder;
    A1C_Decoder_init(&decoder, a1cArena, (A1C_DecoderConfig){});
    A1C_Item* root = A1C_Decoder_decode(
            &decoder,
            reinterpret_cast<const uint8_t*>(serialized.data()),
            serialized.size());
    if (root == nullptr) {
        ALLOC_Arena_freeArena(arena);
        throw Exception("Failed to decode compressor CBOR for patching");
    }
    return root;
}

} // namespace

void TrainedCandidate::replaceBundleID(ZL_BundleID newID)
{
    bundleID = newID;

    if (serializedCompressor.empty()) {
        return;
    }

    std::string bundleIDHex = hexEncodeUniqueID(newID.id);

    Arena* arena = ALLOC_HeapArena_create();
    if (arena == nullptr) {
        throw Exception("replaceBundleID: failed to create arena");
    }
    A1C_Arena a1cArena = A1C_Arena_wrap(arena);
    A1C_Item* root     = decodeCBOR(serializedCompressor, arena, a1cArena);
    A1C_Map rootMap    = root->map;

    A1C_Item* bundleIdItem = A1C_Map_get_cstr(&rootMap, "dict_bundle_id");
    if (bundleIdItem != nullptr) {
        // Replace existing value.
        A1C_Item_string_ref(
                bundleIdItem, bundleIDHex.data(), bundleIDHex.size());

        serializedCompressor = encodeCBOR(root, arena);
        ALLOC_Arena_freeArena(arena);
    } else {
        // Build a new root map with existing entries + dict_bundle_id.
        A1C_Item* newRoot = A1C_Item_root(&a1cArena);
        A1C_MapBuilder newRootBuilder =
                A1C_Item_map_builder(newRoot, rootMap.size + 1, &a1cArena);

        for (size_t i = 0; i < rootMap.size; ++i) {
            A1C_Pair* p = A1C_MapBuilder_add(newRootBuilder);
            if (p == nullptr) {
                ALLOC_Arena_freeArena(arena);
                throw Exception(
                        "replaceBundleID: failed to copy root map entry");
            }
            p->key = rootMap.items[i].key;
            p->val = rootMap.items[i].val;
        }

        A1C_Pair* bundlePair = A1C_MapBuilder_add(newRootBuilder);
        if (bundlePair == nullptr) {
            ALLOC_Arena_freeArena(arena);
            throw Exception("replaceBundleID: failed to add dict_bundle_id");
        }
        A1C_Item_string_refCStr(&bundlePair->key, "dict_bundle_id");
        A1C_Item_string_ref(
                &bundlePair->val, bundleIDHex.data(), bundleIDHex.size());

        serializedCompressor = encodeCBOR(newRoot, arena);
        ALLOC_Arena_freeArena(arena);
    }
}

void TrainedCandidate::replaceDictID(
        const std::vector<ZL_DictID>& oldIDs,
        const std::vector<ZL_DictID>& newIDs)
{
    if (oldIDs.size() != newIDs.size()) {
        throw Exception(
                "replaceDictID: oldIDs and newIDs must have the same size");
    }

    // 1. Update DictEntry structs and packed dict blobs.
    for (size_t i = 0; i < oldIDs.size(); ++i) {
        bool found = false;
        for (auto& entry : dicts) {
            if (ZL_UniqueID_eq(&entry.dictID.id, &oldIDs[i].id)) {
                entry.dictID = newIDs[i];
                if (entry.packedDict.size() >= ZL_DICT_HEADER_SIZE) {
                    std::memcpy(
                            entry.packedDict.data() + 4,
                            newIDs[i].id.bytes,
                            32);
                }
                found = true;
                break;
            }
        }
        if (!found) {
            throw Exception(
                    "replaceDictID: oldID not found in candidate dicts");
        }
    }

    // 2. Patch per-node dict_id values in serialized compressor CBOR.
    if (serializedCompressor.empty() || oldIDs.empty()) {
        return;
    }

    std::map<std::string, std::string> hexReplacements;
    for (size_t i = 0; i < oldIDs.size(); ++i) {
        hexReplacements[hexEncodeUniqueID(oldIDs[i].id)] =
                hexEncodeUniqueID(newIDs[i].id);
    }

    Arena* arena = ALLOC_HeapArena_create();
    if (arena == nullptr) {
        throw Exception("replaceDictID: failed to create arena");
    }
    A1C_Arena a1cArena = A1C_Arena_wrap(arena);
    A1C_Item* root     = decodeCBOR(serializedCompressor, arena, a1cArena);
    A1C_Map rootMap    = root->map;

    A1C_Item* nodesItem = A1C_Map_get_cstr(&rootMap, "nodes");
    if (nodesItem != nullptr) {
        A1C_Map nodesMap = nodesItem->map;
        for (size_t i = 0; i < nodesMap.size; ++i) {
            A1C_Pair* pair       = &nodesMap.items[i];
            A1C_Map nodeMap      = pair->val.map;
            A1C_Item* dictIdItem = A1C_Map_get_cstr(&nodeMap, "dict_id");
            if (dictIdItem == nullptr) {
                continue;
            }

            std::string currentHex(
                    dictIdItem->string.data, dictIdItem->string.size);
            auto it = hexReplacements.find(currentHex);
            if (it != hexReplacements.end()) {
                A1C_Item_string_ref(
                        dictIdItem, it->second.data(), it->second.size());
            }
        }
    }

    serializedCompressor = encodeCBOR(root, arena);
    ALLOC_Arena_freeArena(arena);
}

std::string TrainedCandidate::packBundleInfo() const
{
    if (dicts.empty()) {
        throw Exception("packBundleInfo: no dicts in candidate");
    }

    std::vector<ZL_DictID> dictIDs;
    dictIDs.reserve(dicts.size());
    for (const auto& entry : dicts) {
        dictIDs.push_back(entry.dictID);
    }

    ZL_BundleInfo info;
    std::memset(&info, 0, sizeof(info));
    info.bundleID    = bundleID;
    info.isFatBundle = false;
    info.numDicts    = dictIDs.size();
    info.dictIDs     = dictIDs.data();

    std::string result(
            ZL_BUNDLE_HEADER_SIZE + dicts.size() * ZL_UNIQUE_ID_SIZE, '\0');
    ZL_Report report = BundleInfo_pack(result.data(), result.size(), &info);
    if (ZL_isError(report)) {
        throw Exception("packBundleInfo: BundleInfo_pack failed");
    }

    result.resize(ZL_validResult(report));
    return result;
}

std::string TrainedCandidate::packFatBundle() const
{
    if (dicts.empty()) {
        throw Exception("packFatBundle: no dicts in candidate");
    }

    // Build the arrays that ZL_DictBundle_packFatBundle() expects.
    std::vector<const void*> dictPtrs;
    std::vector<size_t> dictSizes;
    dictPtrs.reserve(dicts.size());
    dictSizes.reserve(dicts.size());

    for (const auto& entry : dicts) {
        dictPtrs.push_back(entry.packedDict.data());
        dictSizes.push_back(entry.packedDict.size());
    }

    // Compute an upper bound for the output buffer:
    //   bundleInfoPackedSize = ZL_BUNDLE_HEADER_SIZE + numDicts *
    //   ZL_UNIQUE_ID_SIZE
    //   + sum of all packed dict sizes
    size_t totalDictBytes = 0;
    for (auto s : dictSizes) {
        totalDictBytes += s;
    }
    size_t capacity = ZL_BUNDLE_HEADER_SIZE + dicts.size() * ZL_UNIQUE_ID_SIZE
            + totalDictBytes;

    std::string result(capacity, '\0');
    ZL_Report report = ZL_DictBundle_packFatBundle(
            result.data(),
            result.size(),
            dictPtrs.data(),
            dictSizes.data(),
            dicts.size());

    if (ZL_isError(report)) {
        throw Exception("packFatBundle: ZL_DictBundle_packFatBundle failed");
    }

    result.resize(ZL_validResult(report));
    return result;
}

} // namespace openzl::training
