// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <zdict.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tools/training/dict/base_dict_trainer.h"

namespace openzl::training {

/**
 * Trainer for zstd dictionaries.
 */
class ZstdDictTrainer : public DictTrainer {
   public:
    ZstdDictTrainer()           = default;
    ~ZstdDictTrainer() override = default;

    std::vector<DictNodeInfo> findDictNodes(
            const Compressor& compressor) override;

    poly::optional<std::string> trainDict(
            const std::vector<MultiInput>& inputs,
            const Compressor& compressor,
            ZL_LocalParams localParams) override;

   private:
    /// Train a raw zstd dictionary from byte spans.
    /// @returns Raw trained dictionary bytes (before content envelope packing).
    std::string trainRawDict(
            const std::vector<poly::span<const uint8_t>>& samples);

    static constexpr size_t maxDictSize_{ 112 * 1024 };
    static constexpr size_t minDictSize_{ ZDICT_DICTSIZE_MIN };
    static constexpr ZDICT_cover_params_t coverParams_{
        .d         = 0,
        .steps     = 256,
        .nbThreads = 16,
    };
};

} // namespace openzl::training
