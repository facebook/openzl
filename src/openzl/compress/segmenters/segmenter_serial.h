// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SEGMENTERS_SERIAL_H
#define ZSTRONG_COMPRESS_SEGMENTERS_SERIAL_H

#include "openzl/codecs/zl_segmenters.h" // ZL_SEGMENT_SERIAL_CHUNK_BYTE_SIZE_PARAM
#include "openzl/shared/portability.h"
#include "openzl/zl_graphs.h" // ZL_StandardGraphID_compress_generic
#include "openzl/zl_segmenter.h"

ZL_BEGIN_C_DECLS

/**
 * Segmenter that accepts serial input and chunks it by byte size.
 * Chunk size is read from local int param
 * ZL_SEGMENT_SERIAL_CHUNK_BYTE_SIZE_PARAM, defaulting to
 * ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE.
 * Each chunk is forwarded to the first custom graph (the successor).
 */
ZL_Report SEGM_serial(ZL_Segmenter* sctx);

#define SEGM_SERIAL_DESC                                           \
    {                                                              \
        .name                = "!zl.segment_serial",               \
        .segmenterFn         = SEGM_serial,                        \
        .inputTypeMasks      = &(const ZL_Type){ ZL_Type_serial }, \
        .numInputs           = 1,                                  \
        .lastInputIsVariable = false,                              \
        .customGraphs =                                            \
                (const ZL_GraphID[]){                              \
                        { ZL_StandardGraphID_compress_generic } }, \
        .numCustomGraphs = 1,                                      \
    }

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_SEGMENTERS_SERIAL_H
