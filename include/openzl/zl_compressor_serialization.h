// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESSOR_SERIALIZATION_H
#define ZSTRONG_COMPRESSOR_SERIALIZATION_H

#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @file
 *
 * This file defines functions related to translating between materialized and
 * serialized representations of a compressor. The materialized representation
 * is of course the `ZL_Compressor` you know and love. The serialized
 * representation is a bytestream with, for the moment, a private, unstable
 * format.
 *
 * There are a number of requirements imposed on the structure and contents of
 * a compressor in order for it to be (de-)serializable:
 *
 * - The format is currently unstable. The serialized compressor must have been
 *   generated from the same library version that is deserializing it, or
 *   deserialization will fail.
 *
 * - Serialization doesn't capture everything. Some components of a compressor
 *   are by their nature non-serializable. These components (custom transforms,
 *   custom graphs, refParams, and ownParams) aren't included in the serialized
 *   compressor, but instead the serialized compressor contains references to
 *   them and expects that the compressor into which it is deserialized will
 *   have those same custom components pre-registered and set up in the same
 *   way. In particular:
 *
 *   - Any custom nodes or graphs in which you provide a function pointer as
 *     part of its registration, i.e., those created by calling:
 *
 *     - ZL_Compressor_registerPipeEncoder
 *     - ZL_Compressor_registerSplitEncoder
 *     - ZL_Compressor_registerTypedEncoder
 *     - ZL_Compressor_registerVOEncoder
 *     - ZL_Compressor_registerMIEncoder
 *
 *     - ZL_Compressor_registerFunctionGraph
 *     - ZL_Compressor_registerFunctionGraph
 *     - ZL_Compressor_registerSerialSelectorGraph
 *     - ZL_Compressor_registerSelectorGraph
 *
 *     are non-serializable. You must pre-register the same component on the
 *     compressor onto which you want to deserialize. Furthermore, since the
 *     serialized graph fundamentally identifies relationships between
 *     components by their names, the component must be registered with the
 *     same explicit(!) name on both the source and destination compressors.
 *
 *     TODO: I plan to expose a method that lets you query a serialized
 *     compressor to find out on which components it depends. So you will
 *     (eventually) be able to query the serialized compressor and then
 *     look up and register the needed components into the compressor before
 *     proceeding with deserialization.
 *
 *   - Any node or graph which you have directly parameterized with any
 *     `ZL_RefParam` params is non-serializable. A node or graph created with
 *     `ZL_RefParam` params must, like components created with custom
 *     functions, be pre-registered with the same explicit name on the
 *     destination compressor.
 *
 *     Note that an otherwise serializable graph component which has exactly
 *     the same `ZL_LocalRefParams` as the component from which it was created
 *     is still serializable. (That is, graph components' `ZL_LocalRefParams`
 *     are inherited from the base component of which they are a copy or
 *     modification, rather than being transported in the serialized
 *     representation.)
 *
 *   - `ZL_CopyParam` params must be semantically flat, serial buffers and
 *     should not contain pointers to external data, since they (when they're
 *     set on serializable components) are serialized. That means that the byte
 *     contents of the buffer `ZL_CopyParam` will be provided to the
 *     corresponding component on the destination compressor, which might be
 *     in a different process which doesn't have access to things pointed-to
 *     outside of that buffer.
 *
 *   - Note however that a copy or modification of a non-serializable object
 *     which does not modify any of the non-serializable attributes is generally
 *     serializable! So while the node resulting from registering a custom
 *     transform or graph is non-serializable, any uses of that graph
 *     component, produced by modifying or composing the original
 *     non-serializable component, via, e.g.:
 *
 *     - ZL_Compressor_cloneNode (see note)
 *
 *     - ZL_Compressor_registerStaticGraph_fromNode1o
 *     - ZL_Compressor_registerStaticGraph_fromPipelineNodes1o
 *     - ZL_Compressor_registerStaticGraph_fromNode
 *     - ZL_Compressor_registerStaticGraph (see note)
 *     - ZL_Compressor_registerParameterizedGraph (see note)
 *
 *     (Note: the marked APIs let you replace the parameters, and so the above
 *     restrictions on what parameters can be modified)
 *
 * All of this is to say, compressors that you would like to serialize and
 * later reinflate should abide by the following structure:
 *
 * Each custom/non-serializable graph component should be registered once in
 * the compressor under a stable, explicit name. Any use or modification of
 * such a component should copy or modify that single explicitly named base
 * version. Those copies and modifications, and the ways they're used in the
 * compressor will be serialized.
 *
 * I recommend avoiding giving those copies / modifications / compositions
 * explicit names. That gives a nice and simple policy:
 *
 * - The root registrations of custom components get explicit names and must
 *   be registered on both the source and destination compressors.
 *
 * - Non-root or non-custom components shouldn't have explicit names and should
 *   only be created on the source compressor, since they will be serialized
 *   and recreated on the destination compressor.
 *
 * Since @ref ZL_Compressor_registerParameterizedGraph lets you replace the
 * successor graphs, even custom graphs can be registered as single,
 * well-isolated objects independent of how they're used in the overall graph.
 * You can then use @ref ZL_Compressor_registerParameterizedGraph to build a
 * serializable graph structure out of those atoms.
 *
 * This approach lets you minimize the parts of the graph that aren't
 * serialized and maximize the parts that are.
 *
 * # Serialization Format:
 *
 * The wire format for serialized compressors is described here. Note that this
 * format is unstable and subject to change at any time.
 *
 * A serialized compressor is a CBOR-encoded object. CBOR is semi-translatable
 * into JSON. This description will use JSON syntax to illustrate the format,
 * but keep in mind this is for illustrative purposes only and the actual
 * format is the CBOR equivalent.
 *
 * The root structure of a serialized compressor is a map with the following
 * key/value pairs:
 *
 * - "version": a required value identifying the version of the serialization
 *   format that this object is using. Currently this is just the OpenZL
 *   library version expressed as an integer. It therefore also identifies the
 *   library version from which this compressor was serialized.
 * - "params": a map, from string to **Param Set**, described below.
 * - "nodes": a map, from string to **Node Description**, described below.
 * - "graphs": a map, from string to **Graph Description**, described below.
 * - "start": a string, identifying the graph in the graphs map to use as the
 *   starting graph.
 * - "global_params": a **Param Set Identifier**, described below, which
 *   identifies the params to set globally. The identified param set must be
 *
 * For the moment, all of these fields must be present, even if they aren't
 * used.
 *
 * TODO: describe the sub-object formats, and complete this format spec...
 */

////////////////////////////////////////
// Serialization
////////////////////////////////////////

/**
 * Creates and initializes an opaque `ZL_CompressorSerializer` object.
 *
 * Currently, this object can only be used for a single call to @ref
 * ZL_CompressorSerializer_serialize(). You need to create a new serializer
 * for every serialization you want to do.
 *
 * This will likely be improved in the future; it shouldn't be too hard to do.
 *
 * @returns the created `ZL_CompressorSerializer` if successful. Otherwise,
 *          returns `NULL`.
 */
ZL_CompressorSerializer* ZL_CompressorSerializer_create(void);

/**
 * Frees all the resources owned by the @p serializer, including the @p
 * serializer itself.
 */
void ZL_CompressorSerializer_free(ZL_CompressorSerializer* serializer);

/**
 * Returns a serialized representation of the given @p compressor.
 *
 * See the documentation above for a description of how compressors must be
 * structured to be compressible.
 *
 * This function uses @p dst and @dstSize both as (1) input arguments that
 * optionally indicate an existing buffer into which the output of the
 * serialization process can be placed as well as (2) output arguments
 * indicating where the output actually was placed.
 *
 * When @p dst points to a `void*` variable with a non-`NULL` initial value,
 * and @p dstSize points to a `size_t` variable with a non-zero initial value,
 * this function will attempt to write the serialized output into the buffer
 * pointed to by `*dst` with capacity `*dstSize`. If the output fits in that
 * provided buffer, then `*dst` will be left unchanged, and `*dstSize` will be
 * updated to reflect the written size of the output.
 *
 * Otherwise, either because the output doesn't fit in the provided buffer or
 * because no buffer was provided (`*dst` is `NULL` or `*dstSize == 0`), an
 * output buffer of sufficient size to hold the output is allocated. `*dst` is
 * set to point to the start of that buffer and `*dstSize` is set to the size
 * of the output. That buffer is owned by @p serializer and will be freed when
 * the @p serializer is destroyed.
 *
 * @param[in out] dst     Pointer to a variable pointing to the output buffer,
 *                        which can start out either pointing to an existing
 *                        output buffer or `NULL`. That variable will be set to
 *                        point to the output buffer actually used.
 * @param[in out] dstSize Pointer to a variable that should be initialized to
 *                        the capacity of the output buffer, if one is being
 *                        provided, or 0 otherwise. That variable will be set
 *                        to contain the written size of the output.
 *
 * @returns success or an error.
 */
ZL_Report ZL_CompressorSerializer_serialize(
        ZL_CompressorSerializer* serializer,
        const ZL_Compressor* compressor,
        void** dst,
        size_t* dstSize);

/**
 * Equivalent @ref ZL_CompressorSerializer_serialize, but produces a human-
 * readable output for debugging. This output format cannot currently be
 * consumed by OpenZL.
 *
 * The output is null-terminated.
 */
ZL_Report ZL_CompressorSerializer_serializeToJson(
        ZL_CompressorSerializer* serializer,
        const ZL_Compressor* compressor,
        void** dst,
        size_t* dstSize);

/**
 * Converts an already-serialized compressor to human-readable JSON.
 *
 * The output is null-terminated.
 *
 * The semantics of @p dst and @p dstSize are as described with @ref
 * ZL_CompressorSerializer_serialize.
 */
ZL_Report ZL_CompressorSerializer_convertToJson(
        ZL_CompressorSerializer* serializer,
        void** dst,
        size_t* dstSize,
        const void* src,
        size_t srcSize);

/**
 * Safely retrieve the full error message associated with an error.
 *
 * @returns the verbose error message associated with the @p result or `NULL`
 *          if the error is no longer valid.
 *
 * @note This string is stored within the @p serializer and is only valid for
 *       the lifetime of the @p serializer.
 */
const char* ZL_CompressorSerializer_getErrorContextString(
        const ZL_CompressorSerializer* serializer,
        ZL_Report result);

/**
 * Like @ref ZL_CompressorSerializer_getErrorContextString(), but generic
 * across result types. Use like:
 *
 * ```
 * ZL_RESULT_OF(Something) result = ...;
 * const char* msg = ZL_CompressorSerializer_getErrorContextString_fromError(
 *     serializer, ZL_RES_error(result));
 * ```
 *
 * @returns the verbose error message associated with the @p error or `NULL`
 *          if the error is no longer valid.
 *
 * @note This string is stored within the @p serializer and is only valid for
 *       the lifetime of the @p serializer.
 */
const char* ZL_CompressorSerializer_getErrorContextString_fromError(
        const ZL_CompressorSerializer* deserializer,
        ZL_Error error);

////////////////////////////////////////
// Deserialization
////////////////////////////////////////

/**
 * Creates and initializes an opaque `ZL_CompressorDeserializer` object.
 *
 * Currently, this object can only be used for a single call to @ref
 * ZL_CompressorDeserializer_deserialize(). You need to create a new
 * deserializer for every deserialization you want to do.
 *
 * This will likely be improved in the future; it shouldn't be too hard to do.
 *
 * @returns the created `ZL_CompressorDeserializer` if successful. Otherwise,
 *          returns `NULL`.
 */
ZL_CompressorDeserializer* ZL_CompressorDeserializer_create(void);

/**
 * Frees all the resources owned by the @p deserializer, including the @p
 * deserializer itself.
 */
void ZL_CompressorDeserializer_free(ZL_CompressorDeserializer* deserializer);

/**
 * Reads the serialized compressor represented by @p serialized and pushes
 * the graph structure and configuration it describes into @p compressor.
 *
 * In order for materialization to succeed, the @p compressor must already have
 * all of the custom transforms, graph functions, selectors, etc. registered
 * that were available when the compressor was serialized. You can use @ref
 * ZL_CompressorDeserializer_getDependencies() to determine what non-serialized
 * graph components are needed on the destination compressor. You can then set
 * those components up before invoking this operation on that compressor.
 *
 * See the documentation above for a more thorough discussion of these
 * requirements and how best to structure a compressor to meet them.
 *
 * If this operation fails, the compressor may be left in an indeterminate
 * state. The best thing to do is to just throw this compressor away
 * (via @ref ZL_Compressor_free) and not to try to re-use it.
 */
ZL_Report ZL_CompressorDeserializer_deserialize(
        ZL_CompressorDeserializer* deserializer,
        ZL_Compressor* compressor,
        const void* serialized,
        size_t serializedSize);

/**
 * Doesn't own any memory.
 */
typedef struct {
    const char* const* graph_names;
    size_t num_graphs;

    const char* const* node_names;
    size_t num_nodes;
} ZL_CompressorDeserializer_Dependencies;

ZL_RESULT_DECLARE_TYPE(ZL_CompressorDeserializer_Dependencies);

/**
 * Read the serialized compressor from @p serialized and find all of the nodes
 * and graphs that the serialized compressor refers to but that aren't present
 * in the serialized compressor. I.e., lists all the components that must be
 * present on a destination compressor in order for this serialized compressor
 * to correctly materialize onto that destination compressor.
 *
 * @param compressor An optional (nullable) pointer to a destination
 *                   compressor. If provided, any nodes or graphs already
 *                   available in that compressor will be removed from the
 *                   returned dependencies, making the returned list strictly
 *                   the *unmet* dependencies. Otherwise, returns all nodes and
 *                   graphs referred to by the serialized compressor but not
 *                   defined by the serialized compressor.
 *
 * @returns The lists of graph and node names which are unsatisfied
 *          dependencies. The memory backing the arrays and strings is owned by
 *          the @p deserializer and will be freed when the @p deserializer is
 *          destroyed.
 */
ZL_RESULT_OF(ZL_CompressorDeserializer_Dependencies)
ZL_CompressorDeserializer_getDependencies(
        ZL_CompressorDeserializer* deserializer,
        const ZL_Compressor* compressor,
        const void* serialized,
        size_t serializedSize);

/**
 * Safely retrieve the full error message associated with an error.
 *
 * @returns the verbose error message associated with the @p result or `NULL`
 *          if the error is no longer valid.
 *
 * @note This string is stored within the @p deserializer and is only valid for
 *       the lifetime of the @p deserializer.
 */
const char* ZL_CompressorDeserializer_getErrorContextString(
        const ZL_CompressorDeserializer* deserializer,
        ZL_Report result);

/**
 * Like @ref ZL_CompressorDeserializer_getErrorContextString(), but generic
 * across result types. Use like:
 *
 * ```
 * ZL_RESULT_OF(Something) result = ...;
 * const char* msg = ZL_CompressorDeserializer_getErrorContextString_fromError(
 *     deserializer, ZL_RES_error(result));
 * ```
 *
 * @returns the verbose error message associated with the @p error or `NULL`
 *          if the error is no longer valid.
 *
 * @note This string is stored within the @p deserializer and is only valid for
 *       the lifetime of the @p deserializer.
 */
const char* ZL_CompressorDeserializer_getErrorContextString_fromError(
        const ZL_CompressorDeserializer* deserializer,
        ZL_Error error);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_COMPRESSOR_SERIALIZATION_H
