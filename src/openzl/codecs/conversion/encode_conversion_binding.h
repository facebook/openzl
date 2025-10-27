// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONVERSION_ENCODE_CONVERSION_BINDING_H
#define ZSTRONG_TRANSFORMS_CONVERSION_ENCODE_CONVERSION_BINDING_H

#include "openzl/codecs/conversion/graph_conversion.h" // *_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"       // ZL_Report
#include "openzl/zl_opaque_types.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

ZL_Report EI_convert_serial_to_struct(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_struct_to_serial(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);

ZL_Report EI_convert_num_to_struct_le(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
// Note : Only works for token sizes 1, 2, 4 or 8
ZL_Report EI_convert_struct_to_num_le(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_struct_to_num_be(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);

// Note : temporary design
// In the future, rather than having multiple variants,
// this will likely be a single transform with a parameter
ZL_Report EI_convert_serial_to_num8(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_serial_to_num_le16(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_serial_to_num_le32(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_serial_to_num_le64(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_serial_to_num_be16(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_serial_to_num_be32(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_convert_serial_to_num_be64(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);

ZL_Report EI_convert_num_to_serial_le(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);

#define EI_CONVERT_NUM_TO_STRUCT_LE(id)           \
    { .gd          = CONVERT_NUM_TOKEN_GRAPH(id), \
      .transform_f = EI_convert_num_to_struct_le, \
      .name        = "!zl.convert_num_to_struct_le" }

#define EI_CONVERT_STRUCT_TO_NUM_LE(id)           \
    { .gd          = CONVERT_TOKEN_NUM_GRAPH(id), \
      .transform_f = EI_convert_struct_to_num_le, \
      .name        = "!zl.convert_struct_to_num_le" }

#define EI_CONVERT_STRUCT_TO_NUM_BE(id)           \
    { .gd          = CONVERT_TOKEN_NUM_GRAPH(id), \
      .transform_f = EI_convert_struct_to_num_be, \
      .name        = "!zl.convert_struct_to_num_be" }

#define EI_CONVERT_SERIAL_TO_NUM8(id)              \
    { .gd          = CONVERT_SERIAL_NUM_GRAPH(id), \
      .transform_f = EI_convert_serial_to_num8,    \
      .name        = "!zl.convert_serial_to_num8" }
#define EI_CONVERT_SERIAL_TO_NUM_LE16(id)           \
    { .gd          = CONVERT_SERIAL_NUM_GRAPH(id),  \
      .transform_f = EI_convert_serial_to_num_le16, \
      .name        = "!zl.convert_serial_to_num_le16" }
#define EI_CONVERT_SERIAL_TO_NUM_LE32(id)           \
    { .gd          = CONVERT_SERIAL_NUM_GRAPH(id),  \
      .transform_f = EI_convert_serial_to_num_le32, \
      .name        = "!zl.convert_serial_to_num_le32" }
#define EI_CONVERT_SERIAL_TO_NUM_LE64(id)           \
    { .gd          = CONVERT_SERIAL_NUM_GRAPH(id),  \
      .transform_f = EI_convert_serial_to_num_le64, \
      .name        = "!zl.convert_serial_to_num_le64" }
#define EI_CONVERT_SERIAL_TO_NUM_BE16(id)           \
    { .gd          = CONVERT_SERIAL_NUM_GRAPH(id),  \
      .transform_f = EI_convert_serial_to_num_be16, \
      .name        = "!zl.convert_serial_to_num_be16" }
#define EI_CONVERT_SERIAL_TO_NUM_BE32(id)           \
    { .gd          = CONVERT_SERIAL_NUM_GRAPH(id),  \
      .transform_f = EI_convert_serial_to_num_be32, \
      .name        = "!zl.convert_serial_to_num_be32" }
#define EI_CONVERT_SERIAL_TO_NUM_BE64(id)           \
    { .gd          = CONVERT_SERIAL_NUM_GRAPH(id),  \
      .transform_f = EI_convert_serial_to_num_be64, \
      .name        = "!zl.convert_serial_to_num_be64" }

#define EI_CONVERT_NUM_TO_SERIAL_LE(id)            \
    { .gd          = CONVERT_NUM_SERIAL_GRAPH(id), \
      .transform_f = EI_convert_num_to_serial_le,  \
      .name        = "!zl.convert_num_to_serial_le" }

#define EI_CONVERT_SERIAL_TO_STRUCT(id)              \
    { .gd          = CONVERT_SERIAL_TOKEN_GRAPH(id), \
      .transform_f = EI_convert_serial_to_struct,    \
      .name        = "!zl.convert_serial_to_struct" }

#define CONVERT_PARAM_TOKENSIZE(l) ZL_LP_1INTPARAM(ZL_trlip_tokenSize, l)

#define EI_CONVERT_SERIAL_TO_STRUCT2(id)             \
    { .gd          = CONVERT_SERIAL_TOKEN_GRAPH(id), \
      .transform_f = EI_convert_serial_to_struct,    \
      .localParams = CONVERT_PARAM_TOKENSIZE(2),     \
      .name        = "!zl.convert_serial_to_struct2" }

#define EI_CONVERT_SERIAL_TO_STRUCT4(id)             \
    { .gd          = CONVERT_SERIAL_TOKEN_GRAPH(id), \
      .transform_f = EI_convert_serial_to_struct,    \
      .localParams = CONVERT_PARAM_TOKENSIZE(4),     \
      .name        = "!zl.convert_serial_to_struct4" }

#define EI_CONVERT_SERIAL_TO_STRUCT8(id)             \
    { .gd          = CONVERT_SERIAL_TOKEN_GRAPH(id), \
      .transform_f = EI_convert_serial_to_struct,    \
      .localParams = CONVERT_PARAM_TOKENSIZE(8),     \
      .name        = "!zl.convert_serial_to_struct8" }

#define EI_CONVERT_STRUCT_TO_SERIAL(id)              \
    { .gd          = CONVERT_TOKEN_SERIAL_GRAPH(id), \
      .transform_f = EI_convert_struct_to_serial,    \
      .name        = "!zl.convert_struct_to_serial" }

/* ===== String - Conversion operations ===== */

/* EI_setStringLens() :
 * convert a serialized input into a string output
 * by adding the string length information.
 * The string length information must be provided
 * either externally, as an array parameter,
 * or via a parser function, as a function pointer parameter.
 */
ZL_Report
EI_setStringLens(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define ZL_SETSTRINGLENS_PARSINGF_PID 520
#define ZL_SETSTRINGLENS_ARRAY_PID 521

#define EI_SETSTRINGLENS(id)                          \
    { .gd          = CONVERT_SERIAL_STRING_GRAPH(id), \
      .transform_f = EI_setStringLens,                \
      .name        = "!zl.private.set_string_lens" }

ZL_Report EI_separate_VSF_components(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);

#define EI_SEPARATE_VSF_COMPONENTS(id)                  \
    { .gd          = SEPARATE_VSF_COMPONENTS_GRAPH(id), \
      .transform_f = EI_separate_VSF_components,        \
      .name        = "!zl.separate_string_components" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_CONVERSION_ENCODE_CONVERSION_BINDING_H
