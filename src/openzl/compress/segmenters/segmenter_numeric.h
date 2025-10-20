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

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_SEGMENTERS_NUMERIC_H
