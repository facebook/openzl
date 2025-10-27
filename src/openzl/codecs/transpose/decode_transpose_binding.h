// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_TRANSPOSE_DECODE_TRANSPOSE_BINDING_H
#define ZSTRONG_TRANSFORMS_TRANSPOSE_DECODE_TRANSPOSE_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"         // PIPE_GRAPH
#include "openzl/codecs/transpose/graph_transpose.h" // TRANSPOSE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder*

ZL_BEGIN_C_DECLS

// Use and generate Fixed-size fields streams
ZL_Report DI_transpose(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_transpose_split(
        ZL_Decoder* dictx,
        const ZL_Input* inFixed[],
        size_t nbInFixed,
        const ZL_Input* inVOs[],
        size_t nbInVOs);

#define DI_TRANSPOSE(id) { .transform_f = DI_transpose, .name = "transpose" }

#define DI_TRANSPOSE_SPLIT(id) \
    { .transform_f = DI_transpose_split, .name = "transpose split" }

/* =============================================
 * LEGACY transforms
 * =============================================
 * preserved for compatibility purposes.
 * They will likely be deprecated at some point in the future.
 * For newer graphs, prefer using the proper TRANSPOSE transform.
 */

/* Note :
 * Individual functions are exposed
 * in order to be used as part of macro initializers
 * which can then be considered "constant" by the compiler.
 *
 * Note that these transforms expect an input as serialized format,
 * which they then interpret as specified (le32, le64),
 * and then produce an output as serialized format.
 * This will change when integer-typed streams will exist.
 */
/* old methods, based on pipe Transform (no longer used) */
size_t
DI_transpose_2(void* dst, size_t dstCapacity, const void* src, size_t srcSize);
size_t
DI_transpose_4(void* dst, size_t dstCapacity, const void* src, size_t srcSize);
size_t
DI_transpose_8(void* dst, size_t dstCapacity, const void* src, size_t srcSize);

/* new methods, based on typedTransform */
ZL_Report DI_transpose2_typed(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_transpose4_typed(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_transpose8_typed(ZL_Decoder* dictx, const ZL_Input* in[]);

ZL_Report DI_transposesplit2_bytes(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_transposesplit4_bytes(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_transposesplit8_bytes(ZL_Decoder* dictx, const ZL_Input* in[]);

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define DI_TRANSPOSE_2(id) { .transform_f = DI_transpose2_typed }

#define DI_TRANSPOSE_4(id) { .transform_f = DI_transpose4_typed }

#define DI_TRANSPOSE_8(id) { .transform_f = DI_transpose8_typed }

#define DI_TRANSPOSE_SPLIT2(id) { .transform_f = DI_transposesplit2_bytes }

#define DI_TRANSPOSE_SPLIT4(id) { .transform_f = DI_transposesplit4_bytes }

#define DI_TRANSPOSE_SPLIT8(id) { .transform_f = DI_transposesplit8_bytes }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_TRANSPOSE_DECODE_TRANSPOSE_BINDING_H
