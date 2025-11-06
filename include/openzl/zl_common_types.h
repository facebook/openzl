// Copyright (c) Meta Platforms, Inc. and affiliates.

/* zs2_common_types.h
 *
 * This header contains some public common types.
 **/

#ifndef ZSTRONG_COMMON_TYPES_H
#define ZSTRONG_COMMON_TYPES_H

#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Generic values for global parameters employing the auto/on/off format.
typedef enum {
    ZL_TernaryParam_auto    = 0,
    ZL_TernaryParam_enable  = 1,
    ZL_TernaryParam_disable = 2
} ZL_TernaryParam;

typedef struct {
    /**
     * Opaque pointer that is passed back to the user when calling functions
     * like:
     * - ZL_Encoder_getOpaquePtr()
     * - ZL_Decoder_getOpaquePtr()
     * - ZL_Graph_getOpaquePtr()
     * - ZL_Selector_getOpaquePtr()
     */
    void* ptr;
    /**
     * Additional pointer passed to the free function.
     * This additional pointer allows, for example, to use a C++ lambda as a
     * free function.
     */
    void* freeOpaquePtr;
    /**
     * Frees the ZL_OpaquePtr::ptr, and if needed also the
     * ZL_OpaquePtr::freeOpaquePtr. This function is called exactly once by
     * OpenZL once the opaque pointer has been registered.
     * If freeFn is NULL, then it is not called.
     */
    void (*freeFn)(void* freeOpaquePtr, void* ptr) ZL_NOEXCEPT_FUNC_PTR;
} ZL_OpaquePtr;

typedef struct {
    const ZL_GraphID* graphids;
    size_t nbGraphIDs;
} ZL_GraphIDList;

typedef struct {
    const ZL_NodeID* nodeids;
    size_t nbNodeIDs;
} ZL_NodeIDList;

/**
 * @brief Data layout for comment contained in the frame header.
 */
typedef struct {
    const void* data;
    size_t size;
} ZL_Comment;

#if defined(__cplusplus)
} // extern "C"
#endif
#endif // ZSTRONG_COMMON_TYPES_H
