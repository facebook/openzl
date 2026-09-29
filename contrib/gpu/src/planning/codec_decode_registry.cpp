// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/planning/codec_decode_registry.hpp"

#include <array>
#include <cstdint>

#include "contrib/gpu/src/codecs/constant/plan_constant_output.hpp"
#include "contrib/gpu/src/codecs/conversion/plan_conversion_output.hpp"
#include "contrib/gpu/src/codecs/float_deconstruct/plan_float_deconstruct_output.hpp"
#include "openzl/common/wire_format.h"

namespace openzl::gpu {
namespace {

/** One version-gated standard transform in the GPU decode registry. */
struct RegistryEntry {
    /// Oldest frame format version supported by this planner, inclusive.
    uint32_t minFormatVersion = 0;
    /// Static host planner, or nullptr when this transform is unsupported.
    CodecDecodePlanningFn* planOutputs = nullptr;
};

constexpr auto kRegistry = [] {
    std::array<RegistryEntry, ZL_StandardTransformID_end> registry{};
    registry[ZL_StandardTransformID_convert_serial_to_struct] = {
        3, planConversionOutputs
    };
    registry[ZL_StandardTransformID_convert_struct_to_serial] = {
        3, planConversionOutputs
    };
    registry[ZL_StandardTransformID_convert_struct_to_num_le] = {
        3, planConversionOutputs
    };
    registry[ZL_StandardTransformID_convert_num_to_struct_le] = {
        3, planConversionOutputs
    };
    registry[ZL_StandardTransformID_convert_serial_to_num_le] = {
        3, planConversionOutputs
    };
    registry[ZL_StandardTransformID_convert_num_to_serial_le] = {
        3, planConversionOutputs
    };
    registry[ZL_StandardTransformID_convert_struct_to_num_be] = {
        21, planConversionOutputs
    };
    registry[ZL_StandardTransformID_convert_serial_to_num_be] = {
        21, planConversionOutputs
    };
    registry[ZL_StandardTransformID_constant_serial]   = { 11,
                                                           planConstantOutputs };
    registry[ZL_StandardTransformID_constant_fixed]    = { 11,
                                                           planConstantOutputs };
    registry[ZL_StandardTransformID_float_deconstruct] = {
        4, planFloatDeconstructOutputs
    };
    return registry;
}();

} // namespace

CodecDecodePlanningFn* findCodecDecodePlanner(
        PublicTransformInfo transform,
        uint32_t formatVersion)
{
    if (transform.trt != trt_standard || transform.trid >= kRegistry.size()) {
        return nullptr;
    }

    const RegistryEntry& entry = kRegistry[transform.trid];
    if (formatVersion < entry.minFormatVersion) {
        return nullptr;
    }
    return entry.planOutputs;
}

} // namespace openzl::gpu
