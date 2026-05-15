// Copyright (c) Meta Platforms, Inc. and affiliates.

/* zs2_ctransform.h
 * Allows creation and integration of Custom Transforms (encoding side).
 * Note that any custom transform employed at compression time
 * requires the reverse decoding transform at decompression time.
 * For the decoding side API, look at zs2_dtransform.h.
 **/

#ifndef ZSTRONG_ZS2_CTRANSFORM_H
#define ZSTRONG_ZS2_CTRANSFORM_H

#include <stddef.h> // size_t
#include "openzl/zl_common_types.h"
#include "openzl/zl_compressor.h"        // ZL_Compressor
#include "openzl/zl_ctransform_legacy.h" // Pipe and Split transforms
#include "openzl/zl_errors.h"            // ZL_Report
#include "openzl/zl_input.h"
#include "openzl/zl_materializer.h" // ZL_MaterializerDesc, ZL_MParam
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_output.h"
#include "openzl/zl_portability.h" // ZL_NOEXCEPT_FUNC_PTR
#include "openzl/zl_selector.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* -------------------------------------------------
 * Transform's declaration & registration
 * -------------------------------------------------*/

/* Transform's State Management (advanced)
 * This can be used to cache a Transform's state within a ZL_CCtx*
 * and keep re-using it across iterations as an optimization
 * in order to save on allocation and initialization costs.
 * Note: currently, a single state is created per Transform.
 * It is then re-used across all compatible nodes present in the Graph.
 *
 * A Transform's state is identified by its 3 components,
 * @stateAlloc, @stateFree and @optionalStateID.
 * If 2 Transforms share the exact same State definition,
 * it's assumed they are compatible and share the same state.
 *
 * @optionalStateID is optional and, if none provided, will be automatically
 * filled by using @transform_f as a key. If you wish 2 different transforms to
 * nonetheless share the same state, provide the same @optionalStateID for both.
 *
 * It's advisable to avoid sticky parameters presumed static across sessions,
 * prefer setting all parameters explicitly at each session,
 * this makes it safer to share states across compatible transforms.
 * If sticky parameters are necessary, ensure that the state will be used for
 * this transform only, by providing a unique @optionalStateID.
 * This unicity must be ensured not just within a single cgraph,
 * but across all cgraph that a same ZL_CCtx* might reference.
 * It's up to the user to ensure ID unicity. When in doubt, do not use the
 * caching system, prefer creating a new state from within the transform.
 */

// Note : these prototypes do not support trampoline allocation functions.
// This is intentional. Future support for such advanced scenario will require
// new prototypes and special care.
typedef void* (*ZL_CodecStateAlloc)(void)ZL_NOEXCEPT_FUNC_PTR;
typedef void (*ZL_CodecStateFree)(void* state) ZL_NOEXCEPT_FUNC_PTR;

typedef struct {
    ZL_CodecStateAlloc stateAlloc;
    ZL_CodecStateFree stateFree;
    size_t optionalStateID; // Optional. Automatically replaced by @transform_f
                            // as a key when none provided.
} ZL_CodecStateManager;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(
        sizeof(void*) == sizeof(size_t),
        "Condition required to ensure H(ZL_CodecStateManager) doesn't depend on padding bytes");
#endif

/* ------------------------------------
 * Typed Transforms
 * ------------------------------------
 * Typed Transforms are roughly equivalent to Split transforms.
 * They accept one Typed buffer and can generate multiple Typed buffers.
 * Selectable buffers' Types are declared in "zs2_data.h".
 *
 * The nb N of outputs is static at declaration time.
 * Once declared, the transform *must* generate as many outputs as declared.
 *
 * The declaration model starts with an @ZL_TypedGraphDesc structure,
 * which explains the split operation and type of the transform, and its ID.
 * It's designed to be shared on both compression and decompression sides.
 *
 * The encoder must require the creation of output streams during its execution,
 * using method @ZL_Encoder_createTypedStream().
 * It's also in charge of determining how many elts it may possibly need to
 * store into each output stream. However, it cannot change the base type
 * (declared in ZL_TypedGraphDesc).
 *
 * The Encoder Interface is also in charge of committing
 * how many elts were actually written into each output stream,
 * using @ZL_Output_commit(), or another write function with included commit.
 * It's allowed to commit `0` bytes.
 * Not committing anything is considered an error.
 *
 * Parameter queries, both global and local, are possible via @ectx.
 *
 * ZL_TypedEncoderFn @return value :
 *          the nb of output streams generated
 *          (necessarily == nb of output streams in Graph Declaration).
 *          An incorrect number will be interpreted as an unexpected error.
 *          Specific error codes can be provided using `zs2_error.h` API.
 **/

#include "openzl/zl_data.h" // ZL_Data, ZL_Type, ZS2_Data_*()

// Custom Typed Transform's signature
typedef ZL_Report (*ZL_TypedEncoderFn)(ZL_Encoder* ectx, const ZL_Input* in)
        ZL_NOEXCEPT_FUNC_PTR;

// Graph Declaration structure for Typed Transforms
// This definition can be shared with the corresponding decoder declaration
// Implementation Note: This structure must look exactly
// identical to the beginning of ZL_VOGraphDesc
typedef struct {
    ZL_IDType CTid;
    ZL_Type inStreamType;
    const ZL_Type* outStreamTypes;
    size_t nbOutStreams; // must be > 0
} ZL_TypedGraphDesc;

// Helper, can be used to declare a list of stream types (optional)
#define ZL_STREAMTYPELIST(...) ZL_GENERIC_LIST(ZL_Type, __VA_ARGS__)

// Typed-stream transform Declaration structure
typedef struct {
    ZL_TypedGraphDesc gd;
    ZL_TypedEncoderFn transform_f;
    ZL_LocalParams localParams;
    const char* name;
    ZL_CodecStateManager trStateMgr;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Encoder_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the compressor.
     */
    ZL_OpaquePtr opaque;
} ZL_TypedEncoderDesc;

/**
 * Register a custom encoder that can be used to compress.
 *
 * @note This is a new variant of @ref ZL_Compressor_registerTypedEncoder that
 * reports errors using OpenZL's ZL_Report error system.
 *
 * @warning Using a custom encoder requires the decoder to be registered before
 * decompression.
 *
 * @param desc The description of the encoder.
 *
 * @returns The new node ID, or an error.
 */
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_registerTypedEncoder2(
        ZL_Compressor* compressor,
        const ZL_TypedEncoderDesc* desc);

/**
 * Register a custom Typed Transform, to be employed in upcoming graph.
 */
ZL_NodeID ZL_Compressor_registerTypedEncoder(
        ZL_Compressor* cgraph,
        const ZL_TypedEncoderDesc* ctd);

/* ------------------------------------
 * Variable Outputs Transforms
 * ------------------------------------
 * Variable Outputs Transforms are roughly equivalent to Typed transforms,
 * but with an ability to declare some of their output streams
 * in any quantity and any order.
 *
 * Possible Outputs Streams must still be declared at registration time,
 * but instead of representing 1 output, that must be generated by the encoder,
 * they now represent a potential output outcome,
 * to which a Successor must be attached (static Graph mode).
 * Output outcomes can then be instantiated as many times as necessary,
 * including 0 times (making them optional).
 *
 * In contrast to Typed Transform,
 * the order in which input Streams are presented to the _decoding_ transform
 * is not described in the Graph declaration.
 * The decoding transform will receive input Streams in the same order as
 * the encoding transform created its output Streams.
 *
 * Stream order is therefore a contract between the encoder and decoder.
 * The decoder must document which stream is compulsory, or optional,
 * and in which order they must be presented, or how they are declared.
 * The encoder will then have to respect this contract
 * by issuing creation of its output streams in the correct order.
 * Usage of Private Headers can be useful to describe complex scenarios.
 *
 * Creation of output streams at encoding time
 * follows the same logic and API as TypedTransform,
 * and therefore employs ZL_Encoder_createTypedStream() and ZL_Output_commit().
 *
 * ZL_VOEncoderFn @return value :
 *          ZL_returnSuccess(),
 *          or an error code, following `zs2_error.h` API.
 **/

// Custom Variable Output Transform's signature
typedef ZL_Report (*ZL_VOEncoderFn)(ZL_Encoder* ectx, const ZL_Input* in)
        ZL_NOEXCEPT_FUNC_PTR;

// Note on ZL_VOEncoderFn :
// the function signature seems identical to TypedTransform_cf,
// though note a difference regarding the @return value's behavior:
// it only returns success or error, not the nb of output streams.
// In the future, this behavior might be expanded to TypedTransforms as well.

// Graph Declaration structure for VO Transforms
// This definition can be shared with the corresponding decoder declaration
// Implementation Note: the beginning of this structure must look exactly
// identical to ZL_TypedGraphDesc
typedef struct {
    ZL_IDType CTid;
    ZL_Type inStreamType;
    const ZL_Type* singletonTypes;
    size_t nbSingletons;
    const ZL_Type* voTypes;
    size_t nbVOs;
} ZL_VOGraphDesc;

// VO Transform Declaration structure
typedef struct {
    ZL_VOGraphDesc gd;
    ZL_VOEncoderFn transform_f;
    ZL_LocalParams localParams;
    const char* name;
    ZL_CodecStateManager trStateMgr;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Encoder_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the compressor.
     */
    ZL_OpaquePtr opaque;
} ZL_VOEncoderDesc;

// Design note 1 :
// This proposal mixes single-instance output streams (singletons)
// with variable ones (VOs).
// All output streams defined as singletons must be instantiated once.
// VO describes output *outcomes*, that can be instantiated multiple times
// including 0 times (optional).
//
// Both types of outputs (singletons and variables) are created
// using the same method ZL_Encoder_createTypedStream().
// In which case, the indexing is unified.
// If @gd contains 1 singleton (indexed 0)
// the second output (index 1) refers to the first VO entry.
// It's recommended to employ enum values to give names to entry indexes.

// Design note 2 : VO order control
// VO Streams will be presented to the decoder
// **in the same order as they were created by the encoder**.
// This is different from singletons,
// which are presented in their declaration order.

/**
 * Register a custom encoder that can be used to compress.
 *
 * @note This is a new variant of @ref ZL_Compressor_registerVOEncoder that
 * reports errors using OpenZL's ZL_Report error system.
 *
 * @warning Using a custom encoder requires the decoder to be registered before
 * decompression.
 *
 * @param desc The description of the encoder.
 *
 * @returns The new node ID, or an error.
 */
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_registerVOEncoder2(
        ZL_Compressor* compressor,
        const ZL_VOEncoderDesc* desc);

ZL_NodeID ZL_Compressor_registerVOEncoder(
        ZL_Compressor* cgraph,
        const ZL_VOEncoderDesc* ctd);

/* ------------------------------------------------------------------------
 * MITransforms: Transforms accepting Multiple Inputs
 * ------------------------------------------------------------------------
 *
 * Extension of the VO Transform, able to ingest multiple input streams.
 */

// MITransform's signature
// Similar to VOTransform, but can receive multiple Inputs.
// When lastInputIsVariable==0,
// @nbInputs is guaranteed to be == MIGraph_Desc.nbInputs.
// When lastInputIsVariable==1, @nbInputs is guaranteed to be
// >= (MIGraph_Desc.nbInputs - 1) , i.e. all inputs before the last one are
// guaranteed to be present. The last input may be present [0-N] times. Any
// additional input beyond the last one has the same type as the last declared
// input.
typedef ZL_Report (*ZL_MIEncoderFn)(
        ZL_Encoder* eictx,
        const ZL_Input* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR;

// Graph Declaration structure for MI Transforms
// This definition can be shared with the corresponding decoder declaration
typedef struct {
    ZL_IDType CTid;
    const ZL_Type* inputTypes;
    size_t nbInputs;         // necessarily >= 1
    int lastInputIsVariable; // note: only last input can be variable.
    const ZL_Type* soTypes;  // Singleton Outputs
    size_t nbSOs;
    const ZL_Type* voTypes; // Variable Outputs
    size_t nbVOs;
} ZL_MIGraphDesc;

// MI Transform Declaration structure
typedef struct {
    ZL_MIGraphDesc gd;
    ZL_MIEncoderFn transform_f;
    ZL_LocalParams localParams;
    const char* name;
    ZL_CodecStateManager trStateMgr;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Encoder_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the compressor.
     */
    ZL_OpaquePtr opaque;
    /**
     * Optional materializer descriptor for materialized local params.
     * If both materializeFn and dematerializeFn are non-null, the materializer
     * will be used to create materialized objects from local params.
     */
    ZL_MaterializerDesc materializer;

    // New API. In progress.
    /**
     * Optional materializer descriptor for materialized dicts.
     * If both materializeFn and dematerializeFn are non-null, the materializer
     * will be used to create materialized objects. Create a node with
     * materialization using ZL_Compressor_parameterizeNode().
     */
    ZL_MaterializerDesc2 dictMat;
    /**
     * Optional dictionary ID associated with this encoder.
     * When set, identifies the dictionary that this encoder requires.
     * A zero-initialized value (ZL_DICT_ID_NULL) means no dictionary is
     * associated.
     */
    ZL_DictID dictID;
    /**
     * Optional materializer for compression-only materialized parameters
     *  (MParams). If materializeFn is non-null, it will be called during
     *  compressor deserialization to create the materialized object from
     *  the serialized MParam blob. Unlike dicts, MParams are NOT required
     *  at decompression time.
     */
    ZL_MaterializerDesc2 mparamMat;
    /**
     * Optional MParam associated with this encoder. The provided content blob
     * will be materialized as dictated by @p mparamMat . OpenZL will not take
     * ownership of the content provided. The caller is free to free the buffer
     * anytime after registering the MIEncoder with
     * ZL_Compressor_registerMIEncoder().
     */
    ZL_MParam mparam;
} ZL_MIEncoderDesc;

/**
 * Register a custom encoder that can be used to compress.
 *
 * @note This is a new variant of @ref ZL_Compressor_registerMIEncoder that
 * reports errors using OpenZL's ZL_Report error system.
 *
 * @warning Using a custom encoder requires the decoder to be registered before
 * decompression.
 *
 * @param desc The description of the encoder.
 *
 * @returns The new node ID, or an error.
 */
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_registerMIEncoder2(
        ZL_Compressor* compressor,
        const ZL_MIEncoderDesc* desc);

ZL_NodeID ZL_Compressor_registerMIEncoder(
        ZL_Compressor* cgraph,
        const ZL_MIEncoderDesc* ctd);

/* ------------------------------------
 * Test
 * ------------------------------------*/

/**
 * Registration might fail, for example if the Descriptor is incorrectly filled.
 * In which case, the returned nodeid is ZL_NODE_ILLEGAL.
 * Any further operation attempted with such a Node will also fail.
 * Such an outcome can be tested with ZL_NodeID_isValid().
 * Note: this is mostly for debugging,
 * once a Descriptor is valid, registration can be assumed to remain successful.
 */
int ZL_NodeID_isValid(ZL_NodeID nodeid);

/* ------------------------------------
 * Transform's Capabilities
 * ------------------------------------*/

/* Consultation request for Global parameters.
 * @return a single Global parameter, identified by @gparam.
 * Note: ZL_CParam is defined within zs2_compress.h
 */
int ZL_Encoder_getCParam(const ZL_Encoder* eic, ZL_CParam gparam);

/* Targeted consultation request of one Local Int parameter.
 * Retrieves the parameter of requested @paramId.
 * If the requested parameter is not present, will return
 * empty parameter with @paramId set to ZL_LP_INVALID_PARAMID
 * and @paramValue set to 0.
 **/
ZL_IntParam ZL_Encoder_getLocalIntParam(const ZL_Encoder* eic, int intParamId);

/* ZL_Encoder_getLocalParam() can be used to access any non-Int parameter,
 * be it copyParam or refParam.
 * In all cases, parameter is presented as a reference (ZL_RefParam).
 */
ZL_RefParam ZL_Encoder_getLocalParam(const ZL_Encoder* eic, int paramId);

/* ZL_Encoder_getLocalCopyParam() is a limited variant, valid only for
 * copy-parameters. In addition to providing a pointer, it also provides the
 * memory size of the parameter in bytes.
 * Note: this entry point might be removed in the future,
 * to avoid building dependency on the size parameter.
 * Prefer using ZL_Encoder_getLocalParam() going forward.
 */
ZL_CopyParam ZL_Encoder_getLocalCopyParam(
        const ZL_Encoder* eic,
        int copyParamId);

/* Bulk consultation request of *all* Local Integer parameters (optimization).
 * This capability can be useful when there are many potential integer
 * parameters, but only a few of them are expected to be present,
 * for example, just a few flags from a very large list.
 */
ZL_LocalIntParams ZL_Encoder_getLocalIntParams(const ZL_Encoder* eic);

/*
 * Bulk consultation request of *all* Local Parameters. This can be useful when
 * one is trying to access all the Local Parameters at once for a codec using
 * the encoder.
 *
 * optional: used during interactive streamdump to report current local
 * parameters (type int, copy, and ref) at compression time.
 */
const ZL_LocalParams* ZL_Encoder_getLocalParams(const ZL_Encoder* eic);

/**
 * @returns The materialized dictionary object associated with this node, if
 * there is one. Returns NULL if there is no dict or if the frame format is
 * less than ZL_MATERIALIZED_DICT_VERSION_MIN.
 */
const void* ZL_Encoder_getMaterializedDict(const ZL_Encoder* eictx);

/**
 * @returns The materialized MParam object associated with this node, if
 * there is one. Otherwise NULL. MParams are compression-only resources
 * that are not required at decompression time.
 */
const void* ZL_Encoder_getMParam(const ZL_Encoder* eictx);

/* Scratch space allocation:
 * When the transform needs some buffer space for some local operation,
 * it can request such space from the Graph Engine. It is allowed to
 * request multiple buffers of any size. Returned buffers are not
 * initialized, and cannot be freed individually. All scratch buffers are
 * automatically released at end of Transform's execution.
 */
void* ZL_Encoder_getScratchSpace(ZL_Encoder* eic, size_t size);

/* ZL_Encoder_createTypedStream():
 * Request creation of an output stream.
 * The Stream Type is already determined by transform's declaration.
 * This operation also creates a buffer, of size eltWidth * eltsCapacity,
 * which can then be written into, using ZL_Output_ptr().
 * The buffer cannot be resized, so reserve appropriate space.
 * It doesn't have to be completely filled though.
 * See ZL_Output_commit() for details.
 *
 * @outcomeIndex: must be valid, corresponding to one of the declared outcomes
 *                starting from 0.
 * @eltWidth: size of fields for ZL_Type_struct and ZL_Type_numeric.
 *            must be ==1 for ZL_Type_serial and ZL_Type_string.
 * @return : valid pointer to ZL_Input* on success,
 *           NULL on failure.
 *
 * Note : the resulting ZL_Input* object can be manipulated
 * using the interface defined in "zstrong/zs2_data.h".
 * Important : note the necessity to employ ZL_Output_commit() at the end
 * to indicate how many elts are effectively written into the output stream.
 * Failure to do so is considered an error.
 **/
ZL_Output* ZL_Encoder_createTypedStream(
        ZL_Encoder* eic,
        int outcomeIndex,
        size_t eltsCapacity,
        size_t eltWidth);

/* Specific for String Type,
 * invoking this method with for an outcome of a different type
 * will result in an error (@return NULL)
 * After creating the Stream, it's possible to access both buffers
 * using ZL_Output_ptr() for the buffer which contains the strings,
 * and ZL_Output_stringLens() for the array which contains the sizes.
 * At the end, specify the nb of Strings using ZL_Output_commit().
 */
ZL_Output* ZL_Encoder_createStringStream(
        ZL_Encoder* eic,
        int outcomeIndex,
        size_t nbStringsMax,
        size_t sumStringLensMax);

// Note : Freeing streams is controlled by the Graph manager.

/* ZL_Encoder_sendCodecHeader() :
 * Set a header content, to be sent out of band (not part of any output stream).
 * The header content will be delivered to the decoding side,
 * assuming the decoder requests it, using ZL_Decoder_getCodecHeader().
 * Only one header (per transform) can be sent.
 * The entire header is presumed sent in a single invocation.
 *
 * Note 1 : this method is intended for *short* out-of-band headers,
 * typically a few bytes long.
 * Large headers, like arrays of integers, are expected to be
 * sent as output streams, for further processing.
 *
 * Note 2 : The decoder will receive exactly the same header data
 * as sent by the encoder preserving the nb and order of bytes.
 * Consequently, any data sent via this chanel must be _serialized_.
 * Don't store native numerical fields via this channel, as byte
 * order of numerical fields depends on local endianness. For multi-bytes
 * numerical values, always set a clear endianness expectation,
 * then send and receive numerical data using this convention only.
 *
 * Note 3 : On short term,
 * the header will be sent "as is", uncompressed, as part of the frame.
 * In the future, it might be compressed, but this will be invisible to the
 * encoder/decoder relation.
 * It will also be transparent and not controllable from the transform.
 *
 * Note 4 : It's possible to send transform headers with
 * splitTransforms, because they feature a ZL_Encoder* state. However, the
 * corresponding splitDecoder does not feature such a state, and as a
 * consequence cannot receive transform's header. On the decoding side, only
 * typedTransforms and above can request transform's header. It's technically
 * possible for splitTransform encoder to be matched by a typedTransform
 * decoder, but it feels weird so it's not recommended. Prefer using
 * typedTransforms for both encoder and decoder sides when employing transform
 * headers.
 *
 * Note 5 : The operation is presumed always successful. If it fails,
 * this will be detected and dealt with at graph level, and remain invisible to
 * the transform. On short term, any failure will essentially abort() the
 * program. Later on, the graph manager will be able to error out without
 * aborting. Failure may happen by calling this function multiple times (only
 * once is allowed) or by sending a too large amount of data as transform's
 * header.
 */
void ZL_Encoder_sendCodecHeader(
        ZL_Encoder* ei,
        const void* trh,
        size_t trhSize);

/**
 * @returns Returns a state, as generated by the Transform's State Manager.
 * The state's lifetime is managed by the host CCtx, it will be free
 * automatically at end of CCtx lifetime (and can't be forcefully free
 * manually). The state may be cached from a previous run with a compatible
 * transform (same state signature).
 */
void* ZL_Encoder_getState(ZL_Encoder* ei);

/**
 * @returns The opaque pointer provided in the transform description.
 * @warning Usage of the opaque pointer must be thread-safe, and must
 * not not modify the state in any way that impacts encoding!
 */
const void* ZL_Encoder_getOpaquePtr(const ZL_Encoder* eictx);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_CTRANSFORM_H
