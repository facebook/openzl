// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_SHARED_COMPONENTS_TENSOR_GRAPHS_H
#define CUSTOM_PARSERS_SHARED_COMPONENTS_TENSOR_GRAPHS_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

/// Classes of segments of a model weight file, each with its own successor.
/// Serialized compressors keep their list of successors, so new classes must
/// be appended: existing values never change.
typedef enum {
    ZL_TensorClass_header = 0,
    ZL_TensorClass_raw, // unknown content: gaps, unsupported types
    ZL_TensorClass_int8,
    ZL_TensorClass_int16,
    ZL_TensorClass_int32,
    ZL_TensorClass_int64,
    ZL_TensorClass_f16,
    ZL_TensorClass_bf16,
    ZL_TensorClass_f32,
    ZL_TensorClass_f64,
    ZL_TensorClass_atypical16, // low-cardinality 16-bit floats
    ZL_TensorClass_atypical32, // low-cardinality 32-bit floats
    ZL_TensorClass_count,
} ZL_TensorClass;

/// Number of elements of the scratch buffer of ZL_TensorGraphs_refineClass().
#define ZL_TENSOR_SAMPLE_SIZE 4096

/// @returns ZL_TensorClass_atypical16 or ZL_TensorClass_atypical32 for an f16,
/// bf16 or f32 tensor with few distinct values (masks, constants...), else
/// @p cls. @p sample is a scratch buffer of ZL_TENSOR_SAMPLE_SIZE elements.
ZL_TensorClass ZL_TensorGraphs_refineClass(
        ZL_TensorClass cls,
        const uint8_t* data,
        size_t size,
        uint32_t* sample);

/// Fills @p successors with the default graph of each class. They are ACE
/// graphs, so that training can replace them.
void ZL_TensorGraphs_buildSuccessors(
        ZL_Compressor* compressor,
        ZL_GraphID successors[ZL_TensorClass_count]);

ZL_END_C_DECLS

#endif
