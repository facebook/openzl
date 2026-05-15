// Copyright (c) Meta Platforms, Inc. and affiliates.

// Public API
// Provide additional capabilities to implement Custom Decoders.
// The Decoder Interface organizes relation between a *raw* custom transform
// function, which can have any implementation, and Zstrong's orchestration
// engine. Note that any custom transform employed at compression time generates
// data that must find its corresponding custom decoder at decompression time.
//
// Note : the following API is provided for the decoding side.
// Look at ctransform.h for the encoding side.

#ifndef ZSTRONG_ZS2_DTRANSFORM_H
#define ZSTRONG_ZS2_DTRANSFORM_H

#include "openzl/zl_common_types.h"
#include "openzl/zl_ctransform.h" // ZL_TypedGraphDesc, ZL_CodecStateManager
#include "openzl/zl_decompress.h" // ZL_DCtx
#include "openzl/zl_dtransform_legacy.h" // Pipe and Split transforms
#include "openzl/zl_errors.h"            // ZL_Report, ZL_isError()
#include "openzl/zl_opaque_types.h"      // ZL_Decoder
#include "openzl/zl_portability.h"       // ZL_NOEXCEPT_FUNC_PTR

#if defined(__cplusplus)
extern "C" {
#endif

/* =================================================================
 * Declaration for custom Typed Decoder Transforms
 * =================================================================
 *
 * This is a mirror of ZL_TypedEncoderDesc within "zs2_ctransform.h" .
 * The encoding side is defined having a single typed stream as input,
 * producing N typed streams as output.
 * Below API declares its corresponding custom decoder,
 * which does the reverse operation, joining N inputs into 1 output.
 *
 * This declaration can then be employed to register the custom decoder
 * into an active Decompression State (DCtx), with
 * ZL_DCtx_registerTypedDecoder().
 *
 * The selected design mimics the encoding side.
 * The output stream must be created from inside the Decoder Interface.
 * The amount of data generated into the destination stream
 * must be committed, using ZL_Output_commit().
 * The function does not have to employ the whole destination buffer,
 * it may use less, but it cannot use more.
 *
 * The @return value is the nb of output streams created (necessarily == 1).
 *
 *
 * Notes on design :
 *
 * Design note 1 : @transform_f assumes that the size of src[] array
 * corresponds to its graph definition in @transform_f.gd .
 * Therefore, there is no "array size" field.
 *
 * Design note 2 :
 * One could imagine using the @return value of @transform_f
 * to tell how many elts were written into the Output Stream.
 * This would skip the need to invoke ZL_Output_commit() within the transform
 * to perform the same operation.
 * Advantage : less easy to forget.
 * Drawback : less consistent with usage pattern of ZL_Input* API.
 *
 * Ultimately, the main benefit of the selected approach
 * is the familiarity with the encoder stage,
 * with both encoder and decoder working approximately the same way.
 * Consistency is what tilted the balance in favor of current design.
 **/

typedef ZL_Report (*ZL_TypedDecoderFn)(ZL_Decoder* dictx, const ZL_Input* src[])
        ZL_NOEXCEPT_FUNC_PTR;

typedef struct {
    ZL_TypedGraphDesc gd; /* same structure as encoder side */
    ZL_TypedDecoderFn transform_f;
    const char* name;
    ZL_CodecStateManager trStateMgr;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Decoder_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the dctx.
     */
    ZL_OpaquePtr opaque;
} ZL_TypedDecoderDesc;

/**
 * Register a custom typed decoder transform.
 * Counterpart to ZS2_registerTypedTransform().
 *
 * Note: Split transforms and Pipe Transforms
 * can be converted into Typed transforms, since it's a strict superset.
 **/
ZL_Report ZL_DCtx_registerTypedDecoder(
        ZL_DCtx* dctx,
        const ZL_TypedDecoderDesc* dtd);

/* =================================================================
 * Declaration for custom VO Decoder Transforms.
 * =================================================================
 *
 * This is the inverse of ZL_VOEncoderDesc within "zs2_ctransform.h",
 * joining N inputs into 1 output.
 *
 * Note : on the decoding side, since it's reversed,
 *        it's the nb of Inputs which is variable.
 *
 * Note 2 : a VOTransform is a strict superset of TypedTransform.
 *
 * The VOTransform decoder API is an extension of TypedTransform decoder,
 * receiving the same Graph Descriptor, to describe compulsory inputs.
 * However, a VOTransform decoder also receives
 * the nb of compulsory streams (O1),
 * and the nb of Variable streams (VO).
 *
 * O1 streams are similar to TypedTransform ones,
 * they are declared in the Graph Descriptor (@.gd),
 * and presented in the order of their declaration.
 *
 * However, VO streams have no such guarantee.
 * They can be presented in any amount and any order,
 * decided by the encoder side.
 * This information is discovered at runtime by the decoder.
 *
 * Stream ordering, if it matters, must be part of an explicit contract.
 * If there are restrictions in the amount and/or order of VO streams.
 * the Decoder Transform must document a corresponding contract
 * to help the encoder implementation to respect the expected ordering.
 *
 * @return: ZL_returnSuccess(),
 *          or an error code, following `zs2_error.h` API.
 *
 * Design note : the return value only returns success,
 *               not the nb of output streams, like in TypedTransform.
 *               In the future, this behavior might be expanded to
 *               TypedTransform as well.
 *
 * Design note 2 : In this proposal,
 *               the decoder discovers which stream type it receives at runtime.
 *               The Graph engine can only check that it's one of the possible
 *               types. It also means that implicit type conversion during
 *               decompression is not possible when there are multiple outcomes
 *               of different types, because the Graph engine has no idea what
 *               is the destination type.
 *               Note : the only benefit of transparent conversions at
 *               decompression time would be a small header size saving.
 **/

typedef ZL_Report (*ZL_VODecoderFn)(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs) ZL_NOEXCEPT_FUNC_PTR;

typedef struct {
    ZL_VOGraphDesc gd; /* same structure as encoder side */
    ZL_VODecoderFn transform_f;
    const char* name;
    ZL_CodecStateManager trStateMgr;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Decoder_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the dctx.
     */
    ZL_OpaquePtr opaque;
} ZL_VODecoderDesc;

/**
 * Register a variable output decoder transform.
 * Counterpart to ZS2_registerVOTransform().
 **/
ZL_Report ZL_DCtx_registerVODecoder(ZL_DCtx* dctx, const ZL_VODecoderDesc* dtd);

/* =================================================================
 * Decoder declaration for custom MI Transforms.
 * =================================================================
 *
 * This is the inverse of ZL_MIEncoderDesc within "zs2_ctransform.h".
 * This decoder can receive multiple inputs, the last one of which can also be
 * variable hence  discovered at runtime. This Transform also inherits the same
 * output capabilities as VO Transforms. It is a superset of VO Transforms.
 *
 * @return: ZL_returnSuccess(),
 *          or an error code, following `zs2_error.h` API.
 **/

typedef ZL_Report (*ZL_MIDecoderFn)(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs) ZL_NOEXCEPT_FUNC_PTR;

typedef struct {
    ZL_MIGraphDesc gd; /* same structure as encoder side */
    ZL_MIDecoderFn transform_f;
    const char* name;
    ZL_CodecStateManager trStateMgr;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Decoder_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the dctx.
     */
    ZL_OpaquePtr opaque;
} ZL_MIDecoderDesc;

/**
 * Register an MI decoder transform.
 * Counterpart to ZS2_registerMITransform().
 **/
ZL_Report ZL_DCtx_registerMIDecoder(ZL_DCtx* dctx, const ZL_MIDecoderDesc* dtd);

/* =================================================================
 * Decoder Transforms capabilities
 * =================================================================
 */
/* Scratch space allocation:
 * When the transform needs some buffer space for some local operation,
 * it can request such space from the Graph Engine. It is allowed to
 * request multiple buffers of any size. Returned buffers are not
 * initialized, and cannot be freed individually. All scratch buffers are
 * automatically released at end of Transform's execution.
 * */
void* ZL_Decoder_getScratchSpace(ZL_Decoder* dictx, size_t size);

/* ZL_Decoder_create1OutStream() :
 * - DTypedTransform only have 1 output stream, they perform a Join operation.
 * - The type of the output stream is already decided in the graph definition,
 *   it can't be changed here.
 * - @eltWidth parameter is compulsory, even for _serial_ type.
 *   Note that, depending on stream types, not all @eltWidth values are allowed.
 *   For example, integer type only allows 1, 2, 4, 8,
 *   and serialized type only allows 1.
 * - Can only be invoked once. Must be invoked once.
 * - Note : don't forget to commit the amount of data written in
 *   the output Stream by invoking ZL_Output_commit() at the end.
 **/
ZL_Output* ZL_Decoder_create1OutStream(
        ZL_Decoder* dictx,
        size_t eltsCapacity,
        size_t eltWidth);

/* Same as above, for String Type specifically.
 * It creates a Stream which contains 2 buffers,
 * one which stores all Strings back-to-back, of capacity @sumStringLensMax,
 * and one for the array of String Lengths, of capacity @nbStringsMax.
 * After creating the Stream, access and write into both buffers
 * using ZL_Output_ptr() for the buffer which contains the strings,
 * and ZL_Output_stringLens() for the array which contains the sizes.
 * At the end, specify the nb of Strings using ZL_Output_commit().
 */
ZL_Output* ZL_Decoder_create1StringStream(
        ZL_Decoder* dictx,
        size_t nbStringsMax,
        size_t sumStringLensMax);

/* ZL_Decoder_createTypedStream():
 * Request creation of an output stream.
 * More general version of ZL_Decoder_create1OutStream().
 * The Stream Type is already determined within Transform's declaration.
 * This operation also creates a buffer, of size eltWidth * eltsCapacity,
 * which can then be written into, using ZL_Output_ptr().
 * The buffer cannot be resized, so reserve appropriate space.
 * It doesn't have to be completely filled though.
 * See ZL_Output_commit() for details.
 *
 * @index: must be valid, corresponding to one of the declared outcomes
 *         starting from 0.
 * @eltWidth: size of fields for ZL_Type_struct and ZL_Type_numeric.
 *            must be ==1 for ZL_Type_serial.
 * @return : valid pointer to ZL_Input* on success,
 *           NULL on failure.
 *
 * Note : the resulting ZL_Input* object can be manipulated
 * using the interface defined in "zstrong/zs2_data.h".
 * Important : note the necessity to employ ZL_Output_commit() at the end
 * to indicate how many elts are effectively written into the output stream.
 * Not doing so is regarded as an error.
 **/
ZL_Output* ZL_Decoder_createTypedStream(
        ZL_Decoder* dictx,
        int index,
        size_t eltsCapacity,
        size_t eltWidth);

/* Specific for String Type,
 * invoking this method for an outcome of a different type
 * will result in an error (@return NULL)
 * After creating the Stream, it's possible to access both buffers
 * using ZL_Output_ptr() for the buffer which contains the strings,
 * and ZL_Output_stringLens() for the array which contains the sizes.
 * At the end, specify the nb of Strings using ZL_Output_commit().
 */
ZL_Output* ZL_Decoder_createStringStream(
        ZL_Decoder* dictx,
        int index,
        size_t nbStringsMax,
        size_t sumStringLensMax);

/* ZL_Decoder_getCodecHeader() :
 * Receives transform's header as sent by the encoding side.
 * The transform's header is not part of any input stream (out of band).
 * It is serialized in essence, so reading any multi-byte numeric values require
 * setting an explicit endian convention.
 **/
ZL_RBuffer ZL_Decoder_getCodecHeader(const ZL_Decoder* dictx);

/**
 * @returns The opaque pointer provided in the transform description.
 * @warning Usage of the opaque pointer must be thread-safe, and must
 * not not modify the state in any way that impacts decoding!
 */
const void* ZL_Decoder_getOpaquePtr(const ZL_Decoder* dictx);

/**
 * @returns Returns a state, as generated by the Transform's State Manager.
 */
void* ZL_Decoder_getState(const ZL_Decoder* dictx);

/**
 * @returns The materialized dictionary object resolved for this node from the
 *          dict loader attached to the active DCtx, if there is one, otherwise
 *          NULL.
 *
 * NOTE: If the frame format < ZL_MATERIALIZED_DICT_VERSION_MIN, this will
 * necessarily return NULL.
 */
const void* ZL_Decoder_getMaterializedDict(const ZL_Decoder* dictx);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_DTRANSFORM_H
