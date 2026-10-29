// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/codecs/float_deconstruct/decode_float_deconstruct_binding.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "openzl/codecs/float_deconstruct/common_float_deconstruct_binding.h"
#include "openzl/common/wire_format.h"

namespace openzl::gpu {
namespace {

bool isBf16FloatDeconstruct(const CodecRunInfo& run)
{
    return run.transform.trt == trt_standard
            && run.transform.trid == ZL_StandardTransformID_float_deconstruct
            && run.codecHeader_h.size() == 1
            && std::to_integer<unsigned>(run.codecHeader_h.front())
            == static_cast<unsigned>(FLTDECON_ElementType_bfloat16)
            && run.inputs.size() == 2 && run.outputs.size() == 1;
}

bool hasBf16Shapes(
        const InputStreamInfo& signFrac,
        const InputStreamInfo& exponent,
        const OutputStreamInfo& output)
{
    const size_t numElts = exponent.shape.numElts;
    return signFrac.shape.type == ZL_Type_struct && signFrac.shape.eltWidth == 1
            && signFrac.shape.numElts == numElts
            && exponent.shape.type == ZL_Type_serial
            && exponent.shape.eltWidth == 1
            && output.shape.type == ZL_Type_numeric
            && output.shape.eltWidth == sizeof(uint16_t)
            && output.shape.numElts == numElts;
}

bool hasBf16Storage(
        const InputStreamInfo& signFrac,
        const InputStreamInfo& exponent,
        const OutputStreamInfo& output)
{
    const size_t numElts = exponent.shape.numElts;
    if (numElts > SIZE_MAX / sizeof(uint16_t)) {
        return false;
    }
    return signFrac.availableStorage.dataBytes >= numElts
            && exponent.availableStorage.dataBytes >= numElts
            && output.storageSize.dataBytes >= numElts * sizeof(uint16_t);
}

} // namespace

bool isBf16CodecRun(const CodecRunInfo& run)
{
    return isBf16FloatDeconstruct(run)
            && hasBf16Shapes(run.inputs[0], run.inputs[1], run.outputs[0])
            && hasBf16Storage(run.inputs[0], run.inputs[1], run.outputs[0]);
}

std::optional<Bf16DeconElementRange> asBf16CodecRun(const BoundCodecRun& run)
{
    if (!isBf16CodecRun(run.info) || run.inputs.size() != 2
        || run.outputs.size() != 1) {
        return std::nullopt;
    }
    const BoundInputStream& signFrac = run.inputs[0];
    const BoundInputStream& exponent = run.inputs[1];
    const BoundOutputStream& output  = run.outputs[0];
    const size_t numElts             = run.info.inputs[1].shape.numElts;
    if (numElts != 0
        && (signFrac.data_d == nullptr || exponent.data_d == nullptr
            || output.data_d == nullptr)) {
        return std::nullopt;
    }
    return Bf16DeconElementRange{
        .exponent = static_cast<const uint8_t*>(exponent.data_d),
        .signFrac = static_cast<const uint8_t*>(signFrac.data_d),
        .dst      = static_cast<uint16_t*>(output.data_d),
        .nbElts   = numElts,
    };
}

} // namespace openzl::gpu
