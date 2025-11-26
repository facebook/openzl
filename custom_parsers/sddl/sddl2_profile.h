// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Sets up SDDL2 profile with clustering for training.
 * Similar to ZL_SDDL_setupProfile but for SDDL2.
 *
 * @param compressor The compressor to register the graph with
 * @param bytecode Pointer to compiled SDDL2 bytecode
 * @param bytecodeSize Size of bytecode in bytes
 * @return ZL_GraphID for the registered graph, or error
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_SDDL2_setupProfile(
        ZL_Compressor* const compressor,
        const void* const bytecode,
        const size_t bytecodeSize);

#if defined(__cplusplus)
}
#endif
