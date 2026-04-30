// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONVERSION_DECODE_CONVERSION_BINDING_H
#define ZSTRONG_TRANSFORMS_CONVERSION_DECODE_CONVERSION_BINDING_H

#include "openzl/codecs/conversion/graph_conversion.h" // TRANSPOSE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder*

ZL_BEGIN_C_DECLS

ZL_Report DI_revert_serial_to_struct(ZL_Decoder* di, const ZL_Input* ins[]);
ZL_Report DI_revert_struct_to_serial(
        ZL_Decoder* di,
        const ZL_Input* ins[]); // not ready yet

ZL_Report DI_revert_num_to_struct_le(ZL_Decoder* di, const ZL_Input* ins[]);
ZL_Report DI_revert_struct_to_num_le(ZL_Decoder* di, const ZL_Input* ins[]);
ZL_Report DI_revert_struct_to_num_be(ZL_Decoder* di, const ZL_Input* ins[]);

ZL_Report DI_revert_serial_to_num_le(ZL_Decoder* di, const ZL_Input* ins[]);
ZL_Report DI_revert_serial_to_num_be(ZL_Decoder* di, const ZL_Input* ins[]);
ZL_Report DI_revert_num_to_serial_le(ZL_Decoder* di, const ZL_Input* ins[]);

#define DI_REVERT_NUM_TO_STRUCT_LE(id)           \
    { .transform_f = DI_revert_num_to_struct_le, \
      .name        = "zl.convert_num_to_struct_le" }

#define DI_REVERT_STRUCT_TO_NUM_LE(id)           \
    { .transform_f = DI_revert_struct_to_num_le, \
      .name        = "zl.convert_struct_to_num_le" }

#define DI_REVERT_STRUCT_TO_NUM_BE(id)           \
    { .transform_f = DI_revert_struct_to_num_be, \
      .name        = "zl.convert_struct_to_num_be" }

#define DI_REVERT_SERIAL_TO_NUM_LE(id)           \
    { .transform_f = DI_revert_serial_to_num_le, \
      .name        = "zl.convert_serial_to_num_le" }

#define DI_REVERT_SERIAL_TO_NUM_BE(id)           \
    { .transform_f = DI_revert_serial_to_num_be, \
      .name        = "zl.convert_serial_to_num_be" }

#define DI_REVERT_NUM_TO_SERIAL_LE(id)           \
    { .transform_f = DI_revert_num_to_serial_le, \
      .name        = "zl.convert_num_to_serial_le" }

#define DI_REVERT_SERIAL_TO_STRUCT(id)           \
    { .transform_f = DI_revert_serial_to_struct, \
      .name        = "zl.convert_serial_to_struct" }

#define DI_REVERT_STRUCT_TO_SERIAL(id)           \
    { .transform_f = DI_revert_struct_to_serial, \
      .name        = "zl.convert_struct_to_serial" }

/* ===== Variable Size Fields - Conversion operations ===== */

ZL_Report DI_revert_VSF_separation(ZL_Decoder* di, const ZL_Input* ins[]);

#define DI_REVERT_VSF_SEPARATION(id)           \
    { .transform_f = DI_revert_VSF_separation, \
      .name        = "separate String components" }

ZL_Report DI_extract_concatenatedFields(ZL_Decoder* di, const ZL_Input* ins[]);

#define DI_REVERT_SETFIELDSIZES(id)                 \
    { .transform_f = DI_extract_concatenatedFields, \
      .name        = "set String lengths" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_CONVERSION_DECODE_CONVERSION_BINDING_H
