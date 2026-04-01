// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_DICT_H
#define OPENZL_DICT_DICT_H

#include "openzl/common/allocation.h"
#include "openzl/dict/sha256.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h" // ZL_NodeID

#if defined(__cplusplus)
extern "C" {
#endif

// ============================================================================
// ZL_Dict - Single Dictionary Structure
// ============================================================================

// Intermediate structure useful for routing the dict to the correct node's
// materializer. Does NOT own any pointers. Ensure the raw dict blob outlives
// this struct.
typedef struct {
    ZL_IDType codecId;
    ZL_SHA256 hash;
    uint32_t dictSize;
    const void* rawDictContent; // NON-owning pointer
} ZL_ParsedDict;
ZL_RESULT_DECLARE_TYPE(ZL_ParsedDict);

/**
 * @brief Single dictionary structure
 *
 * NOTE: ZL_Dict is the MATERIALIZED dictionary object, not the serialized
 * wire representation. It is created from raw dictionary bytes via
 * ZL_Dict_create() or via context-specific materialization functions.
 */
typedef struct {
    ZL_IDType codecId; // Codec responsible for materializing/dematerializing
    ZL_SHA256 hash;    // Precomputed SHA-256 hash of dictContent
    void (*dematerializeFn)(void* dictObj);
    void* dictObj; // Materialized data (caller-allocated or
                   // arena-allocated)
} ZL_Dict;

/**
 * Does basic validation and generates an intermediate representation of the
 * dict blob. Used by the ZL_Compressor and ZL_DCtx, which are expected to then
 * call the proper node materializer to generate the full ZL_Dict structure.
 */
ZL_RESULT_OF(ZL_ParsedDict)
ZL_Dict_parse(
        ZL_OperationContext* opctx,
        const void* dictBlob,
        const size_t blobSize);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_DICT_H
