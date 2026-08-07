// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/codecs/conversion/plan_conversion_output.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "openzl/common/errors_internal.h"
#include "openzl/common/wire_format.h"
#include "openzl/shared/overflow.h"
#include "openzl/shared/varint.h"

namespace openzl::gpu {
namespace {

constexpr StreamInputAlias kWholeInputAlias{
    .inputIndex  = 0,
    .offsetBytes = 0,
};

constexpr bool isNumericWidth(size_t width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

} // namespace

ZL_Report planConversionOutputs(
        const CodecDecodePlanningContext& codec,
        std::span<const StreamPlanningInput> inputs,
        std::span<CodecDecodeOutputPlan> outputs,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    ZL_ERR_IF_NE(inputs.size(), 1, corruption);
    ZL_ERR_IF_NE(outputs.size(), 1, corruption);

    const StreamPlanningInput& input = inputs.front();

    // Validate that the fixed-width input shape fits its available storage.
    ZL_ERR_IF_EQ(input.shape.eltWidth, 0, corruption);
    size_t inputSize;
    ZL_ERR_IF(
            ZL_overflowMulST(
                    input.shape.numElts, input.shape.eltWidth, &inputSize),
            integerOverflow);
    ZL_ERR_IF_LT(input.availableStorage.dataBytes, inputSize, corruption);

    ZL_Type outputType;
    size_t outputNumElts;
    size_t outputEltWidth;
    std::optional<StreamInputAlias> alias;

    // Decoder transforms reverse their encoder-side type conversion. Cases
    // that preserve byte order can alias the complete input stream.
    ZL_ERR_IF_NE(codec.transform.trt, trt_standard, invalidTransform);
    switch (codec.transform.trid) {
        case ZL_StandardTransformID_convert_serial_to_struct:
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_struct, corruption);
            outputType     = ZL_Type_serial;
            outputNumElts  = inputSize;
            outputEltWidth = 1;
            alias          = kWholeInputAlias;
            break;
        case ZL_StandardTransformID_convert_struct_to_serial: {
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_serial, corruption);
            ZL_ERR_IF(codec.codecHeader_h.empty(), srcSize_tooSmall);
            const uint8_t* header = reinterpret_cast<const uint8_t*>(
                    codec.codecHeader_h.data());
            const uint8_t* const headerEnd =
                    header + codec.codecHeader_h.size();
            ZL_TRY_LET_CONST(
                    uint64_t, width, ZL_varintDecode(&header, headerEnd));
            ZL_ERR_IF_NE(header, headerEnd, header_unknown);
            ZL_ERR_IF_EQ(width, 0, header_unknown);
            ZL_ERR_IF_GT(width, SIZE_MAX, integerOverflow);
            outputType     = ZL_Type_struct;
            outputEltWidth = static_cast<size_t>(width);
            ZL_ERR_IF_NE(inputSize % outputEltWidth, 0, corruption);
            outputNumElts = inputSize / outputEltWidth;
            alias         = kWholeInputAlias;
            break;
        }
        case ZL_StandardTransformID_convert_struct_to_num_le:
        case ZL_StandardTransformID_convert_struct_to_num_be:
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_numeric, corruption);
            ZL_ERR_IF_NOT(isNumericWidth(input.shape.eltWidth), corruption);
            outputType     = ZL_Type_struct;
            outputNumElts  = input.shape.numElts;
            outputEltWidth = input.shape.eltWidth;
            if (codec.transform.trid
                        == ZL_StandardTransformID_convert_struct_to_num_le
                || input.shape.eltWidth == 1) {
                alias = kWholeInputAlias;
            }
            break;
        case ZL_StandardTransformID_convert_num_to_struct_le:
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_struct, corruption);
            ZL_ERR_IF_NOT(isNumericWidth(input.shape.eltWidth), corruption);
            outputType     = ZL_Type_numeric;
            outputNumElts  = input.shape.numElts;
            outputEltWidth = input.shape.eltWidth;
            alias          = kWholeInputAlias;
            break;
        case ZL_StandardTransformID_convert_serial_to_num_le:
        case ZL_StandardTransformID_convert_serial_to_num_be:
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_numeric, corruption);
            ZL_ERR_IF_NOT(isNumericWidth(input.shape.eltWidth), corruption);
            outputType     = ZL_Type_serial;
            outputNumElts  = inputSize;
            outputEltWidth = 1;
            if (codec.transform.trid
                        == ZL_StandardTransformID_convert_serial_to_num_le
                || input.shape.eltWidth == 1) {
                alias = kWholeInputAlias;
            }
            break;
        case ZL_StandardTransformID_convert_num_to_serial_le: {
            ZL_ERR_IF_NE(input.shape.type, ZL_Type_serial, corruption);
            ZL_ERR_IF_NE(codec.codecHeader_h.size(), 1, header_unknown);
            const unsigned int widthLog =
                    std::to_integer<unsigned int>(codec.codecHeader_h.front());
            ZL_ERR_IF_GT(widthLog, 3, header_unknown);
            outputEltWidth = size_t{ 1 } << widthLog;
            ZL_ERR_IF_NE(inputSize % outputEltWidth, 0, corruption);
            outputType    = ZL_Type_numeric;
            outputNumElts = inputSize / outputEltWidth;
            alias         = kWholeInputAlias;
            break;
        }
        default:
            ZL_ERR(invalidTransform);
    }

    // Fixed-width conversions preserve total data bytes even when their
    // element boundaries or stream type change.
    const CodecDecodeOutputPlan output{
        .shape = {
                .type     = outputType,
                .numElts  = outputNumElts,
                .eltWidth = outputEltWidth,
        },
        .storageSize = {
                .dataBytes          = inputSize,
                .stringLengthsBytes = 0,
        },
        .alias = alias,
    };
    outputs.front() = output;
    return ZL_returnValue(1);
}

} // namespace openzl::gpu
