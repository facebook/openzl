// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_INTERLEAVE_GRAPH_INTERLEAVE_H
#define OPENZL_CODECS_INTERLEAVE_GRAPH_INTERLEAVE_H

#define INTERLEAVE_STRING_GRAPH(id)                             \
    { .CTid                = id,                                \
      .inputTypes          = ZL_STREAMTYPELIST(ZL_Type_string), \
      .lastInputIsVariable = true,                              \
      .soTypes             = ZL_STREAMTYPELIST(ZL_Type_string) }

#endif // OPENZL_CODECS_INTERLEAVE_GRAPH_INTERLEAVE_H
