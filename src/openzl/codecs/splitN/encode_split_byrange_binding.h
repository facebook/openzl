// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPLITN_ENCODE_SPLIT_BYRANGE_BINDING_H
#define OPENZL_CODECS_SPLITN_ENCODE_SPLIT_BYRANGE_BINDING_H

#include "openzl/codecs/common/graph_vo.h" /* GRAPH_VO_NUM */
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" /* ZL_Encoder */
#include "openzl/zl_errors.h"     /* ZL_Report */
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

/**
 * split_byrange encoder
 *
 * Automatically detects range boundaries in a numeric input stream
 * and splits it into contiguous segments where values belong to
 * non-overlapping ranges.
 *
 * Input: 1 numeric stream (all widths supported: 1, 2, 4, 8 bytes)
 * Output: 1 or more numeric streams (one per detected range segment)
 *
 * Parameters: none (parameter-free node)
 *
 * Wire format: reuses splitN_num transform ID and codec header.
 */
ZL_Report
EI_split_byrange(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_SPLIT_BYRANGE(id)           \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_split_byrange, \
      .name        = "!zl.split_byrange" }

/// Returns the minimum block size used by split_byrange for a given element
/// width (in bytes: 1, 2, 4, or 8).  Block size is the granularity at which
/// min/max statistics are computed.  Tests can use this to size segments
/// appropriately.
size_t ZL_splitByRange_minBlockSize(size_t eltWidth);

ZL_END_C_DECLS

#endif // OPENZL_CODECS_SPLITN_ENCODE_SPLIT_BYRANGE_BINDING_H
