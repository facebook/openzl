// Copyright (c) Meta Platforms, Inc. and affiliates.

// Public Custom Selectors API
// Below API is only required for integration of custom selectors.
// The selector is meant to react to actual input content
// in order to select the next processing stage.
// Note : selectors disappear after their processing and are not present in the
// produced frame. Consequently, they don't need any "reverse selector" at
// decoding time.

#ifndef ZSTRONG_ZS2_SELECTOR_H
#define ZSTRONG_ZS2_SELECTOR_H

#include <stdbool.h> // bool
#include <stddef.h>  // size_t

#include "openzl/zl_common_types.h"
#include "openzl/zl_compress.h" // ZL_CParam
#include "openzl/zl_data.h"     // ZL_Data
#include "openzl/zl_input.h"
#include "openzl/zl_localParams.h"  // ZL_LocalParams
#include "openzl/zl_opaque_types.h" // ZL_GraphID, ZL_Selector
#include "openzl/zl_portability.h"  // ZL_NOEXCEPT_FUNC_PTR

#if defined(__cplusplus)
extern "C" {
#endif

/* ------------------------------------
 * Simple ZL_Type_serial input selector
 * ------------------------------------
 *
 * This is a specialized (yet relatively common) scenario,
 * where the custom selector accepts 1 buffer of bytes (ZL_Type_serial) as
 *input. It then redirects it to another Graph, respecting the same input
 *condition (ZL_Type_serial input)
 *
 * The successor can be any compatible Graph the selector wishes,
 * and can be either Standard of Custom.
 *
 * @customGraphs is only needed to specify a list of Custom Graphs
 * that the selector may choose from as successor at runtime
 * even though the actual GraphID can't be known at compile time
 * (the custom Graph must be registered first, which is done at runtime).
 *
 * When no Custom Graph is needed, the list can be empty,
 * and @customGraphs can be NULL.
 *
 * The content and meaning of @customGraphs array is a contract
 * between the custom selector and its registration operation.
 * To reduce chances of employing an incorrect index,
 * it's recommended to publish a list of `enum` alongside the selector
 * description giving a readable meaning to each index value.
 *
 * The function **MUST** be successful, that's its contract.
 * It has to return a compatible `ZL_GraphID`.
 * It means the selector is _not allowed to fail_.
 * For example, if the selector is unable to make sense of the input data,
 * it should still decide what to do with it.
 * (note that standard LZ codecs can always be used for any input,
 * and therefore make good backup successors).
 *
 * Design note : we could imagine a specific GraphID which could mean "error",
 * then the orchestrator would be allowed to take over
 * and select to do whatever it wants with this input stream.
 * In practice, it will be enough to design a "backup GraphID",
 * which could start its life by delegating to LZ without questioning,
 * and then later integrate some "light" selectors able to distinguish simple
 * situations.
 *
 * Design note 2 : since the selector is allowed to choose any Graph it wants as
 * successor, it's more difficult to ensure that successors are valid at CGraph
 * stage. There are a few ways to deal with this situation :
 * - Force the declaration of successors, even when they are standard nodes.
 *   This way, CGraph can validate that all possible successors are valid.
 *   To ensure that the successor is necessarily part of the list,
 *   it would have to be expressed as an index in this list.
 *   Note that the index could still be wrong (out of range),
 *   requiring the orchestrator to deal with it.
 *   More importantly, an index is another indirection level towards a GraphID,
 *   and a raw index value can be "less expressive" than a Graph name,
 *   making the code more complex to read and maintain.
 * - Detect when a successor is incorrect at orchestrator level,
 *   and decide what to do with this situation.
 *   In debug mode, it's a good reason to abort() and output a decent
 *   explanation. In production mode, we may have to choose between failing,
 *   i.e. bubble up an error, or using a backup node to let the compression
 *   process finish the work, at reduced effectiveness. The toggle between
 *   these modes will likely be triggered by some global parameter. Even in this
 *   last case, it's likely that some sort of log should be filled to explain
 *   that a selector has been behaving incorrectly, allowing some maintenance
 *   process to take over and fix the issue.
 *
 * Note 1 : This variant is only compatible with ZL_Type_serial inputs.
 *          Other types of input (ZL_Type_numeric, ZS2_Type_structs, etc.) are
 *          not compatible.
 *
 * Note 2 : This simple variant can only take decisions based on input's
 *          content. It is unable to query any parameter, neither global nor
 *          local. It is also unable to register some of its analysis as
 *          metadata for successor node. Such increased capability set is
 *          available for TypedSelector.
 **/

typedef ZL_GraphID (*ZL_SerialSelectorFn)(
        const void* src,
        size_t srcSize,
        const ZL_GraphID*
                customGraphs, // list custom Graphs that the selector may choose
                              // as successor. Can be NULL when none needed.
        size_t nbCustomGraphs)
        ZL_NOEXCEPT_FUNC_PTR; // must be zero when customGraphs==NULL

typedef struct {
    ZL_SerialSelectorFn selector_f;
    const ZL_GraphID*
            customGraphs;  // Optional, only needed with custom successors, can
                           // be NULL when none employed
    size_t nbCustomGraphs; // Must be zero when customGraphs==NULL
    const char* name;      // Optional

} ZL_SerialSelectorDesc;

/*
 * Custom Selectors are registered directly as open-ended Graphs.
 * Selectors are free to select any compatible successor Graph,
 * but are also bound to do so by their contract.
 * If a selector returns an incompatible successor,
 * the orchestrator will catch this situation at runtime,
 * and decide what to do, depending on parameters.
 * For example, it may decide to bubble up an error code,
 * or it may substitute the incorrect successor
 * with a generic "backup" successor.
 **/
ZL_GraphID ZL_Compressor_registerSerialSelectorGraph(
        ZL_Compressor* cgraph,
        const ZL_SerialSelectorDesc* csd);

/* =======================================================
 * Selector for Typed streams
 * =======================================================
 * Input is defined as an opaque `const ZL_Input*` object
 * instead of the raw buffer of bytes of the simple variant.
 * This makes it possible to request information about Stream's Type,
 * size and number of its elements.
 *
 * A node context ZL_Selector* is provided too, allowing to request
 * parameters. Methods to request parameters are identical to transforms one,
 * and are therefore defined in zs2_ctransform.h .
 *
 * Note: In the future, it will also provide an entry point to populate
 * @inputStream's metadata, that could be then exploited by successor node,
 * avoiding duplication of analysis.
 */

typedef ZL_GraphID (*ZL_SelectorFn)(
        const ZL_Selector* selectorAPI,
        const ZL_Input* input,
        const ZL_GraphID*
                customGraphs, // list custom Graphs that the selector may choose
                              // as successor. Can be NULL when none needed.
        size_t nbCustomGraphs) // must be zero when customGraphs==NULL
        ZL_NOEXCEPT_FUNC_PTR;

typedef struct {
    ZL_SelectorFn selector_f;
    /**
     * Selectors optionally support multiple input types,
     * using bitmap masking (ex: ZL_Type_struct | ZL_Type_string).
     * In which case, it's the responsibility of the selector to select
     * a successor featuring an input type compatible with current input.
     * Note that it's always preferable to limit Selector's input type
     * to the minimum nb of types possible,
     * because it makes graph validation more accurate and effective.
     */
    ZL_Type inStreamType;
    const ZL_GraphID*
            customGraphs;  // Optional, only needed with custom
                           // successors, can be NULL when none employed
    size_t nbCustomGraphs; // Must be zero when customGraphs==NULL
    ZL_LocalParams localParams;
    /**
     * Optional, the name of the graph rooted by the selector.
     */
    const char* name;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Selector_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the compressor.
     */
    ZL_OpaquePtr opaque;
} ZL_SelectorDesc;

/**
 * Register a selector graph given the @p desc.
 *
 * @note This is a new variant of @ref ZL_Compressor_registerSelectorGraph that
 * reports errors using OpenZL's ZL_Report error system.
 *
 * @param desc The description of the selector, must be non-null.
 *
 * @return The new graph ID, or an error.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_registerSelectorGraph2(
        ZL_Compressor* compressor,
        const ZL_SelectorDesc* desc);

/*
 * Custom selectors are registered directly as open-ended Graphs.
 * Selectors are free to select any compatible successor Graph,
 * but are also bound to do so by their contract.
 * If a selector returns an incompatible successor,
 * the orchestrator will catch this situation at runtime,
 * and decide what to do, depending on parameters.
 * For example, it may decide to bubble up an error code,
 * or it may substitute the incorrect successor
 * with a generic "backup" successor, which always work, though at reduced
 * efficiency.
 */
ZL_GraphID ZL_Compressor_registerSelectorGraph(
        ZL_Compressor* cgraph,
        const ZL_SelectorDesc* ctsd);

/**
 * Utility function to get the input types supported by @gid as an input mask.
 * Throws an error if @gid is not valid or does not have exactly one input.
 * See ZL_Compressor_Graph_getInput0Mask() for more details.
 */
ZL_Type ZL_Selector_getInput0MaskForGraph(
        ZL_Selector const* selCtx,
        ZL_GraphID gid);

const void* ZL_Selector_getOpaquePtr(const ZL_Selector* selector);

/* =======================================================
 * tryGraph:
 * =======================================================
 * This API allows to simulate the execution of a given graphid on an encoder
 * context. Its purpose is to provide a way for Selector to measure how
 * a specific Graph performs and include that information in its
 * decision-making.
 * This API is wasteful in CPU and memory and should be used only when there's
 * no better choice.
 * The API is currently limited and works only for Graphs that accept serialized
 * streams, this limitation will be removed in the future.
 *
 * design note 1 : this API entry point is expected to stay relatively stable.
 *      Its implementation can start with some relatively simple though
 *      heavyweight approach, and then gradually gain some efficiency, For
 *      example, by storing locally compression outcomes, in order to
 *      re-employ them after selection, and then, when applicable, by delegating
 *      to estimation functions, skipping the actual compression job. Later
 *      efficiency improvements could be brought transparently.
 *
 * design note 2 : result is a structure, ZL_GraphReport.
 *      Currently, it only provides final compressed size.
 *      In the future, it may provide additional information,
 *      such as a decompression speed indicator,
 *      or a decompression memory budget.
 *      Changing the content of the structure is an ABI break though,
 *      so that's okay during development,
 *      but as the library gets closer to deployment or public release,
 *      something more stable will be needed.
 *      This could be a ZL_GraphReport* opaque object for example,
 *      to which request methods would be associated.
 *
 * design note 3 : it's unclear at this stage if the `const` property of @selCtx
 *      can be preserved with this design.
 *      It may be possible initially, since nothing will be stored locally,
 *      but may have to change in the future.
 *      In which case, the `const` property of @selCtx will be dropped.
 *      (note: it's currently useful to prevent selectors from requesting
 *      creation of output streams)
 **/
typedef struct {
    ZL_Report finalCompressedSize; // can also transport an error code
} ZL_GraphReport;

ZL_GraphReport ZL_Selector_tryGraph(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        ZL_GraphID graphid);

ZL_Report ZL_Selector_setSuccessorParams(
        const ZL_Selector* selCtx,
        const ZL_LocalParams* lparams);

/* Consultation request for Global parameters.
 * @return a single Global parameter, identified by @gparam.
 * Note: ZL_CParam is defined within zs2_compress.h
 */
int ZL_Selector_getCParam(const ZL_Selector* selCtx, ZL_CParam gparam);

/* Targeted consultation request of one Local Int parameter.
 * Retrieves the parameter of requested @paramId.
 * If the requested parameter is not present, will return
 * empty parameter with @paramId set to ZL_LP_INVALID_PARAMID
 * and @paramValue set to 0.
 **/
#define ZL_LP_INVALID_PARAMID (-1)
ZL_IntParam ZL_Selector_getLocalIntParam(
        const ZL_Selector* selCtx,
        int intParamId);

/* ZL_Selector_getLocalParam() can be used to access any non-Int parameter,
 * be it copyParam or refParam.
 * In all cases, parameter is presented as a reference (ZL_RefParam).
 */
ZL_RefParam ZL_Selector_getLocalParam(const ZL_Selector* selCtx, int paramId);

/* ZL_Selector_getLocalCopyParam() is a limited variant, valid only for
 * copy-parameters. In addition to providing a pointer, it also provides the
 * memory size of the parameter in bytes.
 * Note: this entry point might be removed in the future,
 * to avoid building dependency on the size parameter.
 * Prefer using ZL_Selector_getLocalParam() going forward.
 */
ZL_CopyParam ZL_Selector_getLocalCopyParam(
        const ZL_Selector* selCtx,
        int copyParamId);

/* Bulk consultation request of *all* Local Integer parameters (optimization).
 * This capability can be useful when there are many potential integer
 * parameters, but only a few of them are expected to be present,
 * for example, just a few flags from a very large list.
 */
ZL_LocalIntParams ZL_Selector_getLocalIntParams(const ZL_Selector* selCtx);

/* Scratch space allocation:
 * When the transform needs some buffer space for some local operation,
 * it can request such space from the Graph Engine. It is allowed to
 * request multiple buffers of any size. Returned buffers are not
 * initialized, and cannot be freed individually. All scratch buffers are
 * automatically released at end of Transform's execution.
 */
void* ZL_Selector_getScratchSpace(const ZL_Selector* selCtx, size_t size);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_SELECTOR_H
