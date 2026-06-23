// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <vector>

#include "openzl/zl_opaque_types.h" // ZL_BundleID, ZL_DictID

namespace openzl::training {

/// Represents a trained compressor candidate produced by the training
/// pipeline. Contains the serialized compressor bytes that can be
/// deserialized into a full Compressor object. The serialized data is
/// self-contained and safe to outlive the source Compressor.
struct TrainedCandidate {
    /// Owned serialized compressor bytes (CBOR-encoded).
    std::string serializedCompressor;

    /// The dict bundle ID. Zero/invalid if no dicts were trained.
    ZL_BundleID bundleID{};

    /// Per-dict info: each entry is a (dictID, raw packed dict bytes) pair.
    struct DictEntry {
        ZL_DictID dictID;
        std::string packedDict;
    };
    std::vector<DictEntry> dicts;

    // -----------------------------------------------------------------
    // MC integration helpers
    // -----------------------------------------------------------------

    /// Replace the bundle ID stored on this candidate and patch the
    /// `dict_bundle_id` field in the serialized compressor CBOR.
    void replaceBundleID(ZL_BundleID newID);

    /// Batch-replace dict IDs. For each pair (oldIDs[i], newIDs[i]):
    ///   - updates the matching DictEntry.dictID
    ///   - rewrites bytes 4-35 of the packed dict blob
    ///   - patches the matching per-node `dict_id` in the serialized
    ///     compressor CBOR
    /// @throws openzl::Exception if any oldID is not found or sizes differ.
    void replaceDictID(
            const std::vector<ZL_DictID>& oldIDs,
            const std::vector<ZL_DictID>& newIDs);

    /// Pack all current dicts into a single "fat bundle" blob using the
    /// current bundleID and dict entries. Repurposes
    /// ZL_DictBundle_packFatBundle().
    /// @returns The packed fat bundle bytes.
    std::string packFatBundle() const;

    /// Pack the current bundle metadata without appending dict contents.
    /// @returns The packed standalone BundleInfo bytes.
    std::string packBundleInfo() const;
};

} // namespace openzl::training
