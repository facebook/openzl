// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/codecs/constant/plan_constant_output.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "openzl/common/errors_internal.h"
#include "openzl/common/wire_format.h"
#include "openzl/shared/overflow.h"
#include "openzl/shared/varint.h"

namespace openzl::gpu {

ZL_Report planConstantOutputs(
        const CodecDecodePlanningContext& codec,
        std::span<const StreamPlanningInput> inputs,
        std::span<CodecDecodeOutputPlan> outputs,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    ZL_ERR_IF_NE(inputs.size(), 1, corruption);
    ZL_ERR_IF_NE(outputs.size(), 1, corruption);

    const StreamPlanningInput& input = inputs.front();

    // Validate the single stored value before deriving its expanded output.
    ZL_ERR_IF_NE(input.shape.numElts, 1, corruption);
    ZL_ERR_IF_EQ(input.shape.eltWidth, 0, corruption);
    ZL_ERR_IF_LT(
            input.availableStorage.dataBytes, input.shape.eltWidth, corruption);
    ZL_ERR_IF_NE(codec.transform.trt, trt_standard, invalidTransform);
    switch (codec.transform.trid) {
        case ZL_StandardTransformID_constant_serial:
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_serial, corruption);
            break;
        case ZL_StandardTransformID_constant_fixed:
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_struct, corruption);
            break;
        default:
            ZL_ERR(invalidTransform);
    }

    // The codec header encodes the output element count as a varint.
    ZL_ERR_IF(codec.codecHeader_h.empty(), srcSize_tooSmall);
    const uint8_t* header =
            reinterpret_cast<const uint8_t*>(codec.codecHeader_h.data());
    const uint8_t* const headerEnd = header + codec.codecHeader_h.size();
    ZL_TRY_LET_CONST(
            uint64_t, outputCount, ZL_varintDecode(&header, headerEnd));
    ZL_ERR_IF_NE(header, headerEnd, corruption);
    ZL_ERR_IF_LT(outputCount, 1, corruption);
    ZL_ERR_IF_GT(
            outputCount, std::numeric_limits<size_t>::max(), integerOverflow);

    // Expansion preserves the input type and width while replacing its count.
    size_t dataByteCapacity;
    ZL_ERR_IF(
            ZL_overflowMulST(
                    static_cast<size_t>(outputCount),
                    input.shape.eltWidth,
                    &dataByteCapacity),
            integerOverflow);

    // Constant outputs are materialized as contiguous repeated elements.
    const CodecDecodeOutputPlan output{
        .shape = {
                .type     = input.shape.type,
                .numElts  = static_cast<size_t>(outputCount),
                .eltWidth = input.shape.eltWidth,
        },
        .storageSize = {
                .dataBytes          = dataByteCapacity,
                .stringLengthsBytes = 0,
        },
        .alias = std::nullopt,
    };
    outputs.front() = output;
    return ZL_returnValue(1);
}

} // namespace openzl::gpu
