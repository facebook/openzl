// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DEDUP_ENCODE_DEDUP_BINDING_H
#define ZSTRONG_TRANSFORMS_DEDUP_ENCODE_DEDUP_BINDING_H

#include "openzl/codecs/dedup/graph_dedup.h" // CONCAT_SERIAL_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/* EI_dedup_num:
 * convert all inputs into a single output
 * provided they are all identical Numeric streams.
 */
ZL_Report EI_dedup_num(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_DEDUP_NUM(id)                  \
    { .gd          = DEDUP_NUM_GRAPH(id), \
      .transform_f = EI_dedup_num,        \
      .name        = "!zl.dedup_num" }

// Integer parameter, set to 1 to state that inputs are trusted to be identical
// Note(@Cyan): not part of the public API yet
#define ZL_DEDUP_TRUST_IDENTICAL 9438

/* EI_dedup_num_trusted:
 * same as EI_dedup_num, but trusts that all inputs are identical,
 * so it won't be checked again in the Transform.
 * Use it only in cases where inputs are guaranteed to be identical.
 */
ZL_Report
EI_dedup_num_trusted(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_DEDUP_NUM_TRUSTED(id)           \
    { .gd          = DEDUP_NUM_GRAPH(id),  \
      .transform_f = EI_dedup_num_trusted, \
      .name        = "!zl.private.dedup_num_trusted" }

ZL_END_C_DECLS

#endif
