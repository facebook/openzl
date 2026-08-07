// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/codecs/float_deconstruct/plan_float_deconstruct_output.hpp"

#include <cstddef>
#include <optional>

#include "openzl/codecs/float_deconstruct/common_float_deconstruct_binding.h"
#include "openzl/common/errors_internal.h"
#include "openzl/shared/overflow.h"

namespace openzl::gpu {

ZL_Report planFloatDeconstructOutputs(
        const CodecDecodePlanningContext& codec,
        std::span<const StreamPlanningInput> inputs,
        std::span<CodecDecodeOutputPlan> outputs,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);

    ZL_ERR_IF_NE(codec.transform.trt, trt_standard, invalidTransform);
    ZL_ERR_IF_NE(
            codec.transform.trid,
            ZL_StandardTransformID_float_deconstruct,
            invalidTransform);
    ZL_ERR_IF_NE(inputs.size(), 2, corruption);
    ZL_ERR_IF_NE(outputs.size(), 1, corruption);
    ZL_ERR_IF_NE(codec.codecHeader_h.size(), 1, corruption);
    const unsigned char elementTypeValue =
            std::to_integer<unsigned char>(codec.codecHeader_h.front());
    ZL_ERR_IF_GT(
            elementTypeValue, FLTDECON_ElementTypeEnumMaxValue, corruption);
    const auto elementType =
            static_cast<FLTDECON_ElementType_e>(elementTypeValue);

    const StreamShape& signFrac = inputs[0].shape;
    const StreamShape& exponent = inputs[1].shape;

    // Validate data shapes align
    ZL_ERR_IF_NE(signFrac.type, ZL_Type_struct, corruption);
    ZL_ERR_IF_NE(exponent.type, ZL_Type_serial, corruption);
    ZL_ERR_IF_NE(signFrac.numElts, exponent.numElts, corruption);

    // Validate expected input widths vs actual
    ZL_TRY_LET_CONST(
            size_t, signFracWidth, FLTDECON_SignFracWidth(elementType));
    ZL_TRY_LET_CONST(
            size_t, exponentWidth, FLTDECON_ExponentWidth(elementType));
    ZL_ERR_IF_NE(signFrac.eltWidth, signFracWidth, corruption);
    ZL_ERR_IF_NE(exponent.eltWidth, exponentWidth, corruption);

    // Validates enough available storage
    size_t signFracSize;
    ZL_ERR_IF(
            ZL_overflowMulST(
                    signFrac.numElts, signFrac.eltWidth, &signFracSize),
            integerOverflow);
    ZL_ERR_IF_LT(
            inputs[0].availableStorage.dataBytes, signFracSize, corruption);
    size_t exponentSize;
    ZL_ERR_IF(
            ZL_overflowMulST(
                    exponent.numElts, exponent.eltWidth, &exponentSize),
            integerOverflow);
    ZL_ERR_IF_LT(
            inputs[1].availableStorage.dataBytes, exponentSize, corruption);

    // Reconstruction preserves the element count while restoring the numeric
    // width selected by the codec header.
    ZL_TRY_LET_CONST(size_t, outputWidth, FLTDECON_ElementWidth(elementType));
    size_t dataByteCapacity;
    ZL_ERR_IF(
            ZL_overflowMulST(exponent.numElts, outputWidth, &dataByteCapacity),
            integerOverflow);

    const CodecDecodeOutputPlan output{
        .shape = {
                .type     = ZL_Type_numeric,
                .numElts  = exponent.numElts,
                .eltWidth = outputWidth,
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
