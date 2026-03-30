// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TESTS_DICT_MATERIALIZER_H
#define ZSTRONG_TESTS_DICT_MATERIALIZER_H

#include <map>

#include "openzl/zl_materializer.h"
#include "openzl/zl_opaque_types.h"

struct ZL_MaterializerRegistry_s {
    std::map<ZL_IDType /* codec ID*/, ZL_MaterializerDesc> registry;
};

typedef struct ZL_MaterializerRegistry_s ZL_MaterializerRegistry;

ZL_Report ZL_MaterializerRegistry_registerNode(
        ZL_MaterializerRegistry* registry,
        ZL_MaterializerDesc* desc);
void* ZL_MaterializerRegistry_getMaterializer(
        ZL_MaterializerRegistry* registry,
        ZL_IDType codecId);
void* ZL_MaterializerRegistry_getDematerializer(
        ZL_MaterializerRegistry* registry,
        ZL_IDType codecId);

#endif // ZSTRONG_TESTS_DICT_MATERIALIZER_H
