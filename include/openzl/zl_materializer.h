// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_MATERIALIZER_H
#define OPENZL_ZL_MATERIALIZER_H

#include <stddef.h> // size_t
#include "openzl/zl_common_types.h"
#include "openzl/zl_errors.h"       // ZL_RESULT_OF, ZL_VoidPtr
#include "openzl/zl_localParams.h"  // ZL_LocalParams
#include "openzl/zl_opaque_types.h" // ZL_Materializer
#include "openzl/zl_portability.h"  // ZL_NOEXCEPT_FUNC_PTR

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Descriptor for materializing and dematerializing local params
 *
 * This structure defines functions to materialize an in-memory object from
 * local parameters and to dematerialize (free) that object.
 *
 * Materialized objects are available as a @ref ZL_RefParam via the typical
 * local params access methods. Specify the retrieval key with the paramId
 * field.
 */
typedef struct ZL_MaterializerDesc_s {
    /**
     * @brief A custom function that materializes an in-memory object from a
     * provided @p params object.
     *
     * This function may arbitrarily use none, any, or all of the provided
     * local params to generate the materialized object, but the generation
     * MUST be deterministic and hermetic. In particular, materialization shall
     * not depend on variables other than the provided @ref ZL_LocalParams
     * object.
     *
     * Materialized object lifetimes will be managed by the @ref ZL_Compressor
     * on which the node is registered/parameterized. Objects will be
     * materialized around the time of node registration/parameterization and
     * will remain allocated for the lifetime of the associated @ref
     * ZL_Compressor.
     *
     * Do NOT rely on the materialization function being called at any specific
     * time to do side-effect work. Doing so will result in undefined behavior.
     *
     * The @ref ZL_Compressor may arbitrarily share the same materialized object
     * between multiple nodes with the same @p params and the @ref ZL_CCtx may
     * provide concurrent access to materialized objects. DO NOT attempt to
     * modify the materialized object after creation, either directly or via API
     * getters.
     *
     * @param matCtx A pointer to a materializer context object associated with
     * the @ref ZL_Compressor. The materialization function may use this to
     * request managed memory from the ZL_Compressor as an alternative to
     * managing allocations itself and via the dematerializeFn.
     * @param params  A pointer to the local params object to materialize. The
     * provided params have no lifetime guarantees past the invocation of this
     * function. You may not hold references into the params object in the
     * materialized object.
     *
     * @returns A ZL_RESULT containing a pointer to the materialized object on
     * success, or an error. Returning NULL as a valid result (when there's
     * nothing to materialize) should be wrapped in ZL_WRAP_VALUE(NULL). Ensure
     * the function declares a result scope with ZL_RESULT_DECLARE_SCOPE or you
     * will get a compiler error.
     */
    ZL_RESULT_OF(ZL_VoidPtr) (*materializeFn)(
            ZL_Materializer* matCtx,
            const ZL_LocalParams* params)ZL_NOEXCEPT_FUNC_PTR;

    /**
     * @brief A custom function that destructs a materialized object.
     *
     * You should use this to deallocate all non-arena memory and free any held
     * resources. As a convenience, if there are no resources or memory to free,
     * you may use ZL_NOOP_DEMATERIALIZE as a placeholder.
     */
    void (*dematerializeFn)(ZL_Materializer* matCtx, void* materialized)
            ZL_NOEXCEPT_FUNC_PTR;

    /**
     * The paramId to use for the materialized param. If there is an existing
     * param that uses this paramId, the registration will fail.
     */
    int paramId;

    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Materializer_getOpaquePtr(). OpenZL does not take ownership of this
     * pointer. If lifetime extension is needed, it should be managed by the
     * `ZL_OpaquePtr` in the outer `ZL_MIEncoderDesc`.
     */
    const void* opaque;
} ZL_MaterializerDesc;

/**
 * No-op dematerialization function.
 * Use this as a placeholder when there are no resources or memory to free.
 */
void ZL_NOOP_DEMATERIALIZE(ZL_Materializer* matCtx, void* materialized)
        ZL_NOEXCEPT_FUNC_PTR;

/**
 * Managed space allocation:
 * Materialization may request arena space to hold materialized objects. It is
 * allowed to request multiple buffers of any size. Returned buffers are not
 * initialized, and cannot be freed individually. All buffers are
 * automatically released at end of the owning @ref ZL_Compressor 's lifetime.
 */
void* ZL_Materializer_allocate(ZL_Materializer* matCtx, size_t size);

/**
 * Scratch space allocation:
 * When the materializer needs some buffer space for some local operation,
 * it can request such space from the engine. It is allowed to
 * request multiple buffers of any size. Returned buffers are not
 * initialized, and cannot be freed individually. All scratch buffers are
 * automatically released at the end of the materializer's execution.
 */
void* ZL_Materializer_getScratchSpace(ZL_Materializer* matCtx, size_t size);

// =================================================================
// In-progress API
// =================================================================
/**
 * @brief Descriptor for materializing and dematerializing resource objects
 * (dicts and MParams).
 *
 * Defines functions to create an in-memory object from a raw source buffer
 * (materializeFn) and to free that object (dematerializeFn). Used for both
 * dictionary objects (required at compression and decompression) and MParam
 * objects (compression-only). Note that the registration APIs allow for
 * different materializers for compression-time and decompression-time dict
 * materialization.
 */
typedef struct {
    /**
     * @brief A custom function that materializes an in-memory object from a
     * provided @p src buffer. Separate function interfaces are provided for
     * compression-time and decompression-time materialization. These can be the
     * same function or different functions, depending on the specific codec
     * implementation.
     *
     * The generation MUST be deterministic and hermetic. Materialization shall
     * not depend on variables other than the provided @p src buffer.
     *
     * Materialized object lifetimes will be managed by the @ref ZL_DictLoader
     * or @ref ZL_Compressor on which the materialization scheme is registered.
     *
     * Do NOT rely on the materialization function being called at any specific
     * time to do side-effect work. Doing so will result in undefined behavior.
     *
     * DO NOT attempt to modify the materialized object after creation, either
     * directly or via API getters.
     *
     * @param matCtx A pointer to a materializer context object. The
     * materialization function may use this to request managed memory as an
     * alternative to managing allocations itself and via the dematerializeFn.
     * @param src  A pointer to the buffer from which to materialize. The
     * provided buffer has no lifetime guarantees past the invocation of this
     * function. You may not hold references into @p src in the materialized
     * object.
     *
     * @returns A ZL_RESULT containing a pointer to the materialized object on
     * success, or an error. Returning NULL as a valid result (when there's
     * nothing to materialize) should be wrapped in ZL_WRAP_VALUE(NULL). Ensure
     * the function declares a result scope with ZL_RESULT_DECLARE_SCOPE or you
     * will get a compiler error.
     */
    ZL_RESULT_OF(ZL_VoidPtr) (*materializeFn)(
            ZL_Materializer* matCtx,
            const void* src,
            size_t srcSize)ZL_NOEXCEPT_FUNC_PTR;

    /**
     * @brief A custom function that destructs a materialized object.
     *
     * You should use this to deallocate all non-arena memory and free any held
     * resources. As a convenience, if there are no resources or memory to free,
     * you may use ZL_NOOP_DEMATERIALIZE as a placeholder.
     */
    void (*dematerializeFn)(ZL_Materializer* matCtx, void* materialized)
            ZL_NOEXCEPT_FUNC_PTR;

    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Materializer_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the owning
     * compressor/dict store.
     */
    ZL_OpaquePtr opaque;
} ZL_MaterializerDesc2;

// MParam structure
typedef struct {
    const void* content;
    size_t size;
    /// For advanced use cases, you can specify a custom ID for this MParam. If
    /// unset, a default ID will be assigned.
    ZL_MParamID mparamID;
} ZL_MParam;

/**
 * @returns true if @p id is non-NULL and not ZL_MPARAM_ID_NULL.
 */
bool ZL_MParamID_hasValue(const ZL_MParamID* id);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_ZL_MATERIALIZER_H
