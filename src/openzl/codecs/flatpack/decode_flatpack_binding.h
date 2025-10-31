// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_FLATPACK_DECODE_FLATPACK_BINDING_H
#define ZSTRONG_TRANSFORMS_FLATPACK_DECODE_FLATPACK_BINDING_H

#include "openzl/codecs/flatpack/graph_flatpack.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_flatpack(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_FLATPACK(id) { .transform_f = DI_flatpack, .name = "flatpack" }

#endif
