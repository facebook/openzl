// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_SELECTOR_DECLARE_HELPER_H
#define ZSTRONG_ZS2_SELECTOR_DECLARE_HELPER_H

#include <assert.h> // TODO: remove/replace

#include "openzl/zl_compressor.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"
#include "openzl/zl_selector.h"
#include "openzl/zl_selector_declare_helper_macro_utils.h"

#if defined(__cplusplus)
extern "C" {
#endif

// clang-format off
/*
 * ZL_DECLARE_SELECTOR declares a new selector and creates helper and wrappers
 * that enable better ergonomics and safety for both the developer and user.
 *
 *
 * USAGE
 * ========================
 * The usage for the macro is:
 * ```
 * ZL_DECLARE_SELECTOR(SELECTOR_NAME, STREAM_TYPE,
 *         SUCCESSOR(name [,default_GraphID]),
 *         ...,
 *         SUCCESSOR(name [,default_GraphID]) );
 * ```
 * Each SUCCESSOR has a name, and can get a default GraphID which must be one
 * of the standard GraphIDs (see advanced usages for expectations).
 *
 * Developer API
 * ------------------------
 * The developer *must* implement the selector's logic in a function named
 * `SELECTOR_NAME_impl`. The signature for the implementation function is
 * ```
 * ZL_GraphID SELECTOR_NAME_impl(const ZL_Selector*, const ZL_Input*,
 *        const* SELECTOR_NAME_Successors);
 * ```
 * The third argument for the implementation function is a
 * `SELECTOR_NAME_Successors` struct that is defined by the macro and has a
 * field representing each of the SUCCESSORs, such that that the field's name
 * matched the SUCCESSOR's name. The function must return one of these fields.
 *
 * Graph Level API
 * ------------------------
 * The user can then instantiate the selector in their CGraph by calling:
 * ```
 * SELECTOR_NAME_declareNamedGraph(cgraph,
 *         "selector_graph_name",
 *         SELECTOR_NAME_successors_init(
 *             custom_successor1,
 *             custom_successor2,
 *             ...
 *             custom_successorN));
 * ```
 * In this snippet `custom_successor1` is the successor to use for the first
 * `SUCCESSOR` with no default_GraphID, `custom_successor2` for the second and
 * so on. The user can also specify a name for graph rooted by the selector,
 * in this case the name "selector_graph_name" was chosen.
 * `my_selector_declareNamedGraph` and `my_selector_successors_init` are two
 * helper functions defined by the macro and most users should just use those.
 *
 * Please note that while SUCCESSORs can be given at any arbitrary order,
 * their ordering does determine the order of parameters for
 * the `SELECTOR_NAME_successors_init` function.
 * The user can also override successors with default GraphIDs,
 * see advanced usage for details.
 *
 *
 * EXAMPLE
 * ========================
 * As an example, by using the following declaration:
 * ```
 * ZL_DECLARE_SELECTOR(my_selector, ZL_Type_serial,
 *         SUCCESSOR(custom1), SUCCESSOR(custom2),
 *         SUCCESSOR(flatpack, ZL_GRAPH_FLATPACK));
 * ```
 * We create a new selector named `my_selector`
 * that gets two custom successors
 * and a flatpack successor whose default is ZL_GRAPH_FLATPACK.
 *
 * In order for the selector to work we *must* implement the logic in
 * `my_selector_impl`. The third argument `successors` has the fields
 * `successors->custom1, successors->custom2, successors->flatpack`. If the
 * selector chose to select the second custom graph, it can just return
 * `successors->custom2`. Here's a simple example:
 * ```
 * ZL_GraphID my_selector_impl(const ZL_Selector* ctx,
 *                              const ZL_Input* in,
 *                              const my_selector_Successors* successors)
 * {
 *     // We want the first successor defined by the user
 *     if(custom1_case(in)) return successors->custom1;
 *     // We want the second successor defined by the user
 *     if(custom2_case(in)) return successors->custom2;
 *     // None of the custom successors match, default to flatpack
 *     return successors->flatpack.
 * }
 * ```
 *
 * Then users can use the following snippet to add the selector to a CGraph with
 * the custom successors succ1 and succ2:
 * ```
 * my_selector_declareGraph(cgraph, my_selector_successors_init(succ1, succ2));
 * ```
 *
 *
 * TECHNICAL DETAILS
 * ========================
 * The macro creates the following:
 * - `SELECTOR_NAME_Successors`: a structure that maps each successor to its
 *    matching GraphID.
 * - `SELECTOR_NAME_successors_init`: a functions that initializes and returns a
 *   `SELECTOR_NAME_Successors`, it automatically sets the successors with
 *    default GraphIDs and sets the rest based on user's inputs.
 * - `SELECTOR_NAME_declareGraph`: a function that receives
 *    a `SELECTOR_NAME_Successors`, CGraph and stream type
 *    and declares the selector in this graph,
 *    returns a ZL_GraphID of the newly created graph that begins with the selector.
 * - `SELECTOR_NAME_impl` definition: defines a signature for a
 *   `SELECTOR_NAME_impl` function. This function will host the selector's body
 *   and has the signature:
 *   `ZL_GraphID SELECTOR_NAME_impl(const ZL_Selector*, const ZL_Input*, const* SELECTOR_NAME_Successors)`
 * - `SELECTOR_NAME`: a function that wraps and calls `SELECTOR_NAME_impl`,
 *    should be used as the selector's function.
 *
 * The above example would generate the following code:
 * ```
 * typedef struct {
 *     ZL_GraphID custom1;
 *     ZL_GraphID custom2;
 *     ZL_GraphID flatpack;
 * } my_selector_Successors;
 * ZL_GraphID
 *
 * my_selector_impl(
 *         const ZL_Selector*,
 *         const ZL_Input*,
 *         const my_selector_Successors*);
 *
 * static ZL_GraphID my_selector(
 *         const ZL_Selector* selCtx,
 *         const ZL_Input* input,
 *         const ZL_GraphID* customGraphs,
 *         size_t nbCustomGraphs)
 * {
 *     (void)nbCustomGraphs;
 *     assert(nbCustomGraphs == 3);
 *     void const* _successors = customGraphs;
 *     return my_selector_impl(
 *             selCtx, input, (const my_selector_successors*)_successors);
 * }
 *
 * static my_selector_Successors my_selector_successors_build(
 *         ZL_GraphID custom1,
 *         ZL_GraphID custom2)
 * {
 *     return (my_selector_Successors){ .custom1  = custom1,
 *                                      .custom2  = custom2,
 *                                      .flatpack = ZL_GRAPH_FLATPACK };
 * }
 *
 * static ZL_GraphID my_selector_declareGraph(
 *         ZL_Compressor* cgraph,
 *         my_selector_Successors successors)
 * {
 *     void const* _successors               = &successors;
 *     ZL_SelectorDesc const selector = {
 *         .selector_f     = my_selector,
 *         .inStreamType   = ZL_Type_serial,
 *         .customGraphs   = (ZL_GraphID const*)&_successors,
 *         .nbCustomGraphs = sizeof(successors) / sizeof(ZL_GraphID),
 *     };
 *     return ZL_Compressor_registerSelectorGraph(cgraph, &selector);
 * }
 *
 *
 * ADVANCED USAGE
 * ========================
 * The API provided by this macro provides some flexibility of usage and
 * advanced use cases. These use cases are not recommended, but can be used
 * if the situation requires.
 *
 * 1. Using a non standard GraphD as a default_GraphID - our only requirement
 * for a default GraphId is that it'd be an expression that evaluates into a
 * GraphID. As the macro doesn't get a context, this would almost always be a
 * standard GraphID. Still, it is possible to provide a global or even a
 * function call that return a GraphID that is assigned in runtime. While this
 * should work, it's not explicitly supported or tested and not recommended.
 *
 * 2. Overriding default GraphIDs - in some cases we might be interested in
 * overriding the selector's default GraphIDs with other GraphID. One such use
 * case can be to replace all of the selector's successors with mocks for
 * testing purposes. This override can be achieved by assigning different values
 * to the selector_Successors struct that is used as a third argument to
 * `my_selector_declareGraph`.
 *
 */
// clang-format on
#define ZL_DECLARE_SELECTOR(SELECTOR_NAME, STREAM_TYPE, ...)                \
    typedef struct {                                                        \
        _ZS2_SELECTOR_STRUCT_DEFINITION(__VA_ARGS__)                        \
    } SELECTOR_NAME##_Successors;                                           \
    ZL_GraphID SELECTOR_NAME##_impl(                                        \
            const ZL_Selector*,                                             \
            const ZL_Input*,                                                \
            const SELECTOR_NAME##_Successors*) ZL_NOEXCEPT_FUNC_PTR;        \
    static ZL_UNUSED_ATTR ZL_GraphID SELECTOR_NAME(                         \
            const ZL_Selector* selCtx,                                      \
            const ZL_Input* input,                                          \
            const ZL_GraphID* customGraphs,                                 \
            size_t nbCustomGraphs) ZL_NOEXCEPT_FUNC_PTR                     \
    {                                                                       \
        (void)nbCustomGraphs;                                               \
        assert(nbCustomGraphs == _ZS2__NARG__(__VA_ARGS__));                \
        void const* _successors = customGraphs;                             \
        return SELECTOR_NAME##_impl(                                        \
                selCtx,                                                     \
                input,                                                      \
                (const SELECTOR_NAME##_Successors*)_successors);            \
    }                                                                       \
    static ZL_UNUSED_ATTR SELECTOR_NAME##_Successors                        \
    SELECTOR_NAME##_successors_init(_ZS2_SELECTOR_INIT_ARGS(__VA_ARGS__))   \
    {                                                                       \
        return (SELECTOR_NAME##_Successors){ _ZS2_SELECTOR_INIT_SET(        \
                __VA_ARGS__) };                                             \
    }                                                                       \
    static ZL_UNUSED_ATTR ZL_GraphID SELECTOR_NAME##_declareNamedGraph(     \
            ZL_Compressor* cgraph,                                          \
            const char* const name,                                         \
            const SELECTOR_NAME##_Successors successors)                    \
    {                                                                       \
        void const* _successors        = &successors;                       \
        ZL_SelectorDesc const selector = {                                  \
            .selector_f     = SELECTOR_NAME,                                \
            .inStreamType   = STREAM_TYPE,                                  \
            .customGraphs   = (ZL_GraphID const*)_successors,               \
            .nbCustomGraphs = sizeof(successors) / sizeof(ZL_GraphID),      \
            .name           = name,                                         \
        };                                                                  \
        return ZL_Compressor_registerSelectorGraph(cgraph, &selector);      \
    }                                                                       \
    static ZL_UNUSED_ATTR ZL_GraphID SELECTOR_NAME##_declareGraph(          \
            ZL_Compressor* cgraph,                                          \
            const SELECTOR_NAME##_Successors successors)                    \
    {                                                                       \
        return SELECTOR_NAME##_declareNamedGraph(cgraph, NULL, successors); \
    }

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_SELECTOR_DECLARE_HELPER_H
