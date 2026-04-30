// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SEGMENTERS_NUMERIC_H
#define ZSTRONG_COMPRESS_SEGMENTERS_NUMERIC_H

#include "openzl/shared/portability.h"
#include "openzl/zl_segmenter.h"

ZL_BEGIN_C_DECLS

ZL_Report SEGM_numeric(ZL_Segmenter* sctx);

#define SEGM_NUMERIC_DESC                                           \
    {                                                               \
        .name                = "!zl.segmenter_numeric",             \
        .segmenterFn         = SEGM_numeric,                        \
        .inputTypeMasks      = &(const ZL_Type){ ZL_Type_numeric }, \
        .numInputs           = 1,                                   \
        .lastInputIsVariable = false,                               \
    }

/* ---- Serial-input numeric segmenter ---- */

/**
 * Segmenter that accepts serial input and chunks it by byte size,
 * aligned to a configurable element width.
 * Chunk size and element width are read from local int params.
 * Each chunk is forwarded to the first custom graph.
 */
ZL_Report SEGM_numFromSerial(ZL_Segmenter* sctx);

/* Local param IDs */
#define SEGM_NUM_FROM_SERIAL_CHUNK_BYTE_SIZE_ID 300
#define SEGM_NUM_FROM_SERIAL_ELT_WIDTH_ID 301

/* Default chunk size: shared segmenter default */
#define SEGM_NUM_FROM_SERIAL_DEFAULT_CHUNK_SIZE \
    ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE

#define SEGM_NUM_FROM_SERIAL_DESC(eltWidth, widthBits, defaultGraphEnumID) \
    {                                                               \
        .name                = "!zl.segment_num" #widthBits "_from_serial", \
        .segmenterFn         = SEGM_numFromSerial,                  \
        .inputTypeMasks      = &(const ZL_Type){ ZL_Type_serial },  \
        .numInputs           = 1,                                   \
        .lastInputIsVariable = false,                               \
        .customGraphs    = (const ZL_GraphID[]){ { defaultGraphEnumID } }, \
        .numCustomGraphs = 1,                                       \
        .localParams = {                                            \
            .intParams = {                                          \
                .intParams = (const ZL_IntParam[]){                 \
                    { .paramId    = SEGM_NUM_FROM_SERIAL_ELT_WIDTH_ID, \
                      .paramValue = (eltWidth) },                   \
                },                                                  \
                .nbIntParams = 1,                                   \
            },                                                      \
        },                                                          \
    }

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_SEGMENTERS_NUMERIC_H
