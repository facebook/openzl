// Copyright (c) Meta Platforms, Inc. and affiliates.

/* zs2_compressor.h
 * Design notes
 *
 * This API makes it possible for users to define their own Custom Graph.
 *
 * Note that this is different from creating custom nodes,
 * which are defined in zs2_ctransform.h and zs2_selector.h .
 *
 * Consequently, when using only this header,
 * users can only generate graph using only Standard nodes.
 */

#ifndef ZSTRONG_ZS_CGRAPH_API_H
#define ZSTRONG_ZS_CGRAPH_API_H

#include <stdbool.h>                // bool
#include <stddef.h>                 // size_t
#include "openzl/zl_compress.h"     // ZL_CCtx
#include "openzl/zl_errors.h"       // ZL_Report, ZL_ErrorCode
#include "openzl/zl_localParams.h"  // ZL_LocalParams
#include "openzl/zl_opaque_types.h" // ZL_GraphID
#include "openzl/zl_portability.h"  // ZL_NOEXCEPT_FUNC_PTR
#include "openzl/zl_public_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @defgroup Group_Compressor Compressor
 *
 * The `ZL_Compressor` object is the core concept of the OpenZL compression
 * engine.
 *
 * A OpenZL compression graph is built out of graphs and nodes.
 *
 * - **graph**: Compresses one or more inputs. All graphs are built out of
 * nodes, except for the unit graph `ZL_GRAPH_STORE` which takes one input and
 * stores it in the compressed frame. OpenZL also provides many commonly used
 * pre-built graphs in `zl_public_nodes.h`. Custom graphs can also be
 * constructed using the @ref ZL_Compressor object. Graphs are compression-time
 * only, so custom graphs can be added any time without impacting the
 * decompressor.
 * - **node**: Transforms one or more inputs into one or more outputs. OpenZL
 *   provides many builtin standard nodes in `zl_public_nodes.h`. Users can also
 *   register custom nodes, however this requires registering a decoder with the
 *   @ref ZL_DCtx in order to decode the data.
 *
 * @{
 */

/**
 * @defgroup Group_Compressor_LifetimeManagement Lifetime Management
 *
 * @{
 */

typedef struct ZL_Compressor_s ZL_Compressor;

/**
 * @brief Create a new @ref ZL_Compressor.
 *
 * The @ref ZL_Compressor must be freed with @ref ZL_Compressor_free.
 *
 * @returns The @ref ZL_Compressor pointer or `NULL` on error.
 */
ZL_Compressor* ZL_Compressor_create(void);

/**
 * @brief Frees a @ref ZL_Compressor.
 *
 * If @p compressor is `NULL` this function does nothing.
 *
 * @param compressor The @ref ZL_Compressor to free or `NULL`.
 */
void ZL_Compressor_free(ZL_Compressor* compressor);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_Errors Errors and Warnings
 *
 * During graph creation, it is common to accidentally build invalid graphs. For
 * example, by hooking up nodes with incompatible types. Graph creation will
 * ultimately fail during graph validation, at which point a `ZL_Report` will be
 * returned. On an error [ZL_Compressor_getErrorContextString][] will return
 * detailed information about the error and where the error occurred.
 * [ZL_Compressor_getWarnings][] will return a list of warnings, or earlier
 * errors for even more details.
 *
 * @{
 */

/**
 * @returns A verbose error string containing context about the error that
 * occurred. This is useful for debugging, and for submitting bug reports to
 * OpenZL developers.
 * @note This string is stored within the @p compressor and is only valid for
 * the lifetime of the @p compressor.
 */
char const* ZL_Compressor_getErrorContextString(
        ZL_Compressor const* compressor,
        ZL_Report report);

/**
 * See @ref ZL_Compressor_getErrorContextString()
 *
 * The same as ZL_Compressor_getErrorContextString() except works on a @ref
 * ZL_Error.
 */
char const* ZL_Compressor_getErrorContextString_fromError(
        ZL_Compressor const* compressor,
        ZL_Error error);

/**
 * @returns The array of warnings that were encountered during the creation
 * of the compressor.
 * @note The array's and the errors' lifetimes are valid until the next non-
 * const call on the compressor.
 */
ZL_Error_Array ZL_Compressor_getWarnings(const ZL_Compressor* compressor);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_Parameterization Parameterization
 *
 * Compression parameters can be attached to compressors.
 * These parameters are used to configure the behavior of compression, e.g. by
 * setting the compression level or format version. These parameters can be
 * overridden by parameters set on a `ZL_CCtx`.
 *
 * @{
 */

/**
 * @brief Set global parameters via @p compressor. In this construction, global
 * parameters are attached to a Compressor object. Global Parameters set at
 * Compressor level can be overridden later at CCtx level.
 *
 * @returns Success or an error which can be checked with ZL_isError().
 * @param gcparam The global parameter to set.
 * @param value The value to set for the global parameter.
 */
ZL_Report ZL_Compressor_setParameter(
        ZL_Compressor* compressor,
        ZL_CParam gcparam,
        int value);

/**
 * @brief Read a parameter's configured value in the Compressor and returns it.
 *
 * @returns Returns the value of the parameter if it is set, or 0 if unset.
 * @param gcparam The global parameter to read.
 */
int ZL_Compressor_getParameter(
        const ZL_Compressor* compressor,
        ZL_CParam gcparam);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_StaticGraphCreation Static Graph Creation
 *
 * There are two types of graphs in OpenZL: static graphs and dynamic graphs.
 * Static graphs take a single input, pass that input to a codec, and the
 * outputs of that codec are sent to the successor graphs. Dynamic graphs are
 * graphs that inspect the input at runtime to make different decisions. These
 * are either function graphs or selectors.
 *
 * This API allows the construction of static graphs. The head node and the
 * successor graphs must be specified. Additionally, a name can be provided for
 * the graph, which can aid in debugging. Finally, the graph can be
 * parameterized, which sends the local parameters to the head node.
 *
 * The main function is @ref ZL_Compressor_buildStaticGraph. The other functions
 * are older variants that will eventually be removed.
 * @{
 */

typedef struct {
    /// Optionally a name for the graph for debugging.
    /// If NULL, then the static graph will not have a name.
    const char* name;
    /// Optionally local parameters to pass to the head node.
    /// If NULL, then the head node's local parameters will not be overridden.
    const ZL_LocalParams* localParams;
} ZL_StaticGraphParameters;

/**
 * Build a new graph out of pre-existing components. The new graph passes its
 * data to @p headNode, and then each output of @p headNode is set to the
 * corresponding @p successorGraph.
 *
 * @param headNode Pass the input data to this node
 * @param successorGraphs Pass the outputs of @p headNode to these graphs
 * @param numSuccessorGraphs Number of successor graphs
 * @param params Optionally extra parameters for the static graph, or NULL.
 *
 * @returns Thew new graph ID, or an error.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildStaticGraph(
        ZL_Compressor* compressor,
        ZL_NodeID headNode,
        const ZL_GraphID* successorGraphs,
        size_t numSuccessorGraphs,
        const ZL_StaticGraphParameters* params);

/**
 * @brief Create a graph from a single input & output node.
 *
 * Simplified variant of @ref ZL_Compressor_registerStaticGraph_fromNode that
 * only works for nodes that have one input and one output. Creates a new graph
 * headed by
 * @p headNode, whose output gets sent to @p dstGraph.
 *
 * @returns The newly created graph or `ZL_GRAPH_ILLEGAL` on error.
 * The user may check for errors using ZL_GraphID_isValid().
 *
 * @param headNode The node executed first in the newly created graph.
 * @param dstGraph The graph that will receive the output of @p headNode.
 */
ZL_GraphID ZL_Compressor_registerStaticGraph_fromNode1o(
        ZL_Compressor* compressor,
        ZL_NodeID headNode,
        ZL_GraphID dstGraph);

/**
 * @brief Creates a graph consisting of a series of nodes executed in succession
 * in the order provided and then sent to @p dstGraph.
 *
 * @returns The newly created graph or `ZL_GRAPH_ILLEGAL` on error.
 * The user may check for errors using ZL_GraphID_isValid().
 *
 * @param nodes The nodes to execute in the newly created graph.
 * @param nbNodes The number of nodes in @p nodes.
 * @param dstGraph The graph that will receive the output of the last node in @p
 * nodes.
 */
ZL_GraphID ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
        ZL_Compressor* compressor,
        const ZL_NodeID* nodes,
        size_t nbNodes,
        ZL_GraphID dstGraph);

/**
 * @brief C11 Helper, to define a list of Nodes as compound literals.
 * Example:
 * ZL_Compressor_registerStaticGraph_fromPipelineNodes1o( compressor,
 *                                   ZL_NODELIST(node1, node2, node3),
 *                                   finalGraph)
 */
#define ZL_NODELIST(...) ZL_GENERIC_LIST(ZL_NodeID, __VA_ARGS__)

/**
 * @brief Create a graph from a head node.
 *
 * Creates a new graph headed by @p headNode, which produces
 * @p nbDstGraphs outcomes. Each outcome of @p headNode gets sent
 * to the corresponding graph in @p dstGraphs.
 *
 * @param headNode The head node in the newly created graph.
 * @param dstGraphs Array of graphs of size @p nbDstGraphs.
 * @param nbDstGraphs Must be equal to the number of outputs of @p headNode.
 *
 * @returns The newly created graph or `ZL_GRAPH_ILLEGAL` on error.
 * The user may check for errors using ZL_GraphID_isValid().
 *
 * @note Successor dstGraphs can only be employed in single-input mode.
 * Multi-input Graphs can only be invoked from a function graph.
 */
ZL_GraphID ZL_Compressor_registerStaticGraph_fromNode(
        ZL_Compressor* compressor,
        ZL_NodeID headNode,
        const ZL_GraphID* dstGraphs,
        size_t nbDstGraphs);

/**
 * @brief C11 Helper, to define a list of Graphs as compound literals.
 * Example :
 * ZL_Compressor_registerStaticGraph_fromNode(compressor, startingNodeID,
 *                  ZL_GRAPHLIST(graph1, graph2, graph3) )
 **/
#define ZL_GRAPHLIST(...) ZL_GENERIC_LIST(ZL_GraphID, __VA_ARGS__)

typedef struct {
    const char* name; // optional
    ZL_NodeID headNodeid;
    const ZL_GraphID* successor_gids;
    size_t nbGids;
    const ZL_LocalParams* localParams; // optional
} ZL_StaticGraphDesc;

/**
 * This is the more complete declaration variant, offering more control and
 * capabilities.
 * In order to be valid, a Static Graph Description must :
 * - provide exactly as many successors as nb of outcomes defined by head Node
 * - only employ single-input Graphs as successors
 * - match each outcome type with a successor using a compatible input type
 * - optionally, can specify @localParams for a Static Graph. In this case,
 *   these parameters are forwarded to the Head Node, replacing any previous
 *   local parameter that may have been already set on the Head Node.
 *
 * If a declaration is invalid, it results in an invalid GraphID, which can be
 * tested using ZL_GraphID_isValid() on the return value.
 * Note: ZL_GraphID_isValid() is currently defined in zs2_graph_api.h.
 */
ZL_GraphID ZL_Compressor_registerStaticGraph(
        ZL_Compressor* compressor,
        const ZL_StaticGraphDesc* sgDesc);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_NodeCustomization Node Customization
 *
 * Nodes can be customized to override their name and local parameters.
 * This is an advanced use case, and mainly an implementation detail of nodes.
 * Most nodes that accept parameters provide helper functions to correctly
 * parameterize the node.
 *
 * @{
 */

typedef struct {
    /// Optionally a new name, if NULL it is derived from the node's name
    const char* name;
    /// Optionally the new local params, if NULL then the parameters are not
    /// updated.
    const ZL_LocalParams* localParams;
} ZL_NodeParameters;

/**
 * Parameterize an existing node by overriding its name and/or local parameters.
 *
 * @param node The node to parameterize.
 * @param params The new parameters, which must be non-null.
 *
 * @returns The new node ID on success, or an error.
 */
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_parameterizeNode(
        ZL_Compressor* compressor,
        ZL_NodeID node,
        const ZL_NodeParameters* params);

typedef struct {
    /// Optionally a new name, if NULL it is derived from the node's name
    const char* name;
    /// Node to parameterize
    ZL_NodeID node;
    /// Optionally the new local params, if NULL then the parameters are not
    /// updated.
    const ZL_LocalParams* localParams;
} ZL_ParameterizedNodeDesc;

/**
 * @brief Clone an existing @ref ZL_NodeID from an existing node, but
 * optionally with a new name & new parameters.
 *
 * @param desc The parameterization options.
 *
 * @returns The new node id of the cloned node.
 */
ZL_NodeID ZL_Compressor_registerParameterizedNode(
        ZL_Compressor* compressor,
        const ZL_ParameterizedNodeDesc* desc);

/**
 * @brief Simplified variant of @ref ZL_Compressor_registerParameterizedNode().
 * Clone an existing @ref ZL_NodeID from an already registered
 * @p nodeid but employs new parameters, set via @p localParams.
 *
 * @returns The new node id of the cloned node.
 *
 * @param nodeid The node to clone.
 * @param localParams The local parameters to use for the node.
 **/
ZL_NodeID ZL_Compressor_cloneNode(
        ZL_Compressor* compressor,
        ZL_NodeID nodeid,
        const ZL_LocalParams* localParams);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_GraphCustomization Graph Customization
 *
 * Graphs can be customized to override their name, local parameters, custom
 * nodes and custom graphs. This is an advanced use case, and mainly an
 * implementation detail of graphs. Most graphs which accept parameters provide
 * helper functions to correctly parameterize the graph.
 *
 * @{
 */

typedef struct ZL_GraphParameters_s {
    /// Optional, for debug traces, otherwise it is derived from graph's name.
    const char* name;
    /// Empty means don't override
    const ZL_GraphID* customGraphs;
    size_t nbCustomGraphs;
    /// Empty means don't override
    const ZL_NodeID* customNodes;
    size_t nbCustomNodes;
    /// NULL means don't override
    const ZL_LocalParams* localParams;
} ZL_GraphParameters;

/**
 * Parameterizes an existing graph by overriding its name, customGraphs,
 * customNodes, and/or localParams.
 *
 * @param graph The graph to parameterize.
 * @param params The new parameters, which must be non-null.
 *
 * @returns The new graph ID on success, or an error.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_parameterizeGraph(
        ZL_Compressor* compressor,
        ZL_GraphID graph,
        const ZL_GraphParameters* params);

/**
 * This creates a new Graphs, based on an existing Graph,
 * but modifying all or parts of its exposed parameters.
 */

typedef struct {
    /// Optionally a new name, otherwise it is derived from `graph`'s name.
    const char* name;
    ZL_GraphID graph;
    /// Empty means don't override
    const ZL_GraphID* customGraphs;
    size_t nbCustomGraphs;
    /// Empty means don't override
    const ZL_NodeID* customNodes;
    size_t nbCustomNodes;
    /// NULL means don't override
    const ZL_LocalParams* localParams;
} ZL_ParameterizedGraphDesc;

/**
 * @brief Create a new GraphID by the one from @p gid,
 * just replacing the @p localParams by the provided ones. Used to create
 * custom variants of Standard Graphs for example.
 *
 * @note the original @gid still exists and remains accessible.
 * @note @localParams==NULL means "do not change the parameters",
 *       in which case, this function simply returns @gid.
 *
 * @return The GraphID of the newly created graph, or ZL_GRAPH_ILLEGAL on
 * error.
 *
 * @param gid The GraphID to clone.
 * @param localParams The local parameters to use inside the graph.
 */
ZL_GraphID ZL_Compressor_registerParameterizedGraph(
        ZL_Compressor* compressor,
        const ZL_ParameterizedGraphDesc* desc);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_GraphComponentLookup Graph Component Lookup
 *
 * Any registered node or graph that has an explicit name can be queried using
 * these lookup functions. See [names.md](/api/names.md) for details.
 *
 * @{
 */

/**
 * @brief Lookup a node by name.
 *
 * Looks up a node with the given name and returns it. Anchor nodes (nodes whose
 * name starts with '!') can be looked up by name, excluding the leading '!'.
 * Standard nodes can also be looked up by name. Non-anchor nodes are assigned a
 * unique name by suffixing them with `#${unique}`. They can be looked up if you
 * know the unique name.
 *
 * @returns The node if it exists, or ZL_NODE_ILLEGAL.
 */
ZL_NodeID ZL_Compressor_getNode(
        const ZL_Compressor* compressor,
        const char* name);

/**
 * @brief Lookup a graph by name.
 *
 * Looks up a graph with the given name and returns it. Anchor graphs (graphs
 * whose name starts with '!') can be looked up by name, excluding the leading
 * '!'. Standard graphs can also be looked up by name. Non-anchor graphs are
 * assigned a unique name by suffixing them with `#${unique}`. They can be
 * looked up if you know the unique name.
 *
 * @returns The graph if it exists, or ZL_GRAPH_ILLEGAL.
 */
ZL_GraphID ZL_Compressor_getGraph(
        const ZL_Compressor* compressor,
        const char* graph);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_GraphFinalization Graph Finalization
 *
 * After creation, the graph is finalized with
 * [ZL_Compressor_selectStartingGraphID][]. At this point any errors during
 * graph creation that haven't already been caught will be surfaced.
 *
 * @{
 */

/**
 * @brief Selects a graph as the default entry point for the compressor.
 *
 * By default, a compressor's entry point is its most recently registered graph.
 * This function allows explicit selection of a different graph as the default
 * entry point for subsequent compression operations.
 *
 * @param compressor The compressor instance to configure. Must not be NULL.
 * @param graph The graph ID to set as the default entry point. Must be a valid
 *              graph ID that has been registered with this compressor.
 * @returns ZL_Report indicating success or failure. Use ZL_isError() to check
 *          for errors.
 *
 * @note The compressor can still be used as a collection of multiple entry
 * points. Alternative entry points can be selected at runtime using
 *       ZL_CCtx_selectStartingGraphID().
 * @note This operation automatically validates the compressor by calling
 *       ZL_Compressor_validate() internally.
 *
 * See ZL_CCtx_selectStartingGraphID() for runtime entry point selection
 */
ZL_Report ZL_Compressor_selectStartingGraphID(
        ZL_Compressor* compressor,
        ZL_GraphID graph);

/**
 * @brief Validates a graph maintains basic invariants to reduce the chance of
 * errors being triggered when compressing.
 *
 * @note this operation is also integrated as part of
 * ZL_Compressor_selectStartingGraphID(). This function is kept for backward
 * compatibility.
 *
 * @returns Success if graph is valid, error otherwise.
 * @param starting_graph The starting graph to validate.
 */
ZL_Report ZL_Compressor_validate(
        ZL_Compressor* compressor,
        ZL_GraphID starting_graph);

/**
 * @}
 */

/**
 * @defgroup Group_Compressor_ReferenceCompressor Reference Compressor
 *
 * To use a compressor to compress an input, call [ZL_CCtx_refCompressor][].
 *
 * @{
 */

/**
 * @brief Pass @p compressor as a `ZL_Compressor*` object to the compression
 * state. Compression will start with the default Starting GraphID of @p
 * compressor, using its default parameters provided at registration time.
 * @note Only one compressor can be referenced at a time.
 *       Referencing a new compressor deletes previous reference.
 * @note If a custom GraphID and parameters were previously set,
 *       invoking this method will reset them to default.
 * @pre @p compressor must remain valid for the duration of its usage.
 * @pre @p compressor must be already validated.
 */
ZL_Report ZL_CCtx_refCompressor(ZL_CCtx* cctx, const ZL_Compressor* compressor);

/**
 * @brief Set the starting Graph of next compression,
 * as @p graphID referenced in the provided @p compressor,
 * optionally providing it with some runtime parameters.
 * @pre @p compressor must remain valid for the duration of its usage.
 * @pre @p compressor must be already validated.
 *
 * @param cctx The active compression state
 * @param compressor The reference compressor containing graph definitions.
 *                   If NULL, it uses the currently registered compressor.
 * @param graphID The ID of the starting graph.
 * @param rgp Optional parameters to apply to the starting graph.
 *            NULL means don't override.
 *
 * Note: like all global parameters, these parameters are reset at end of
 * compression.
 */
ZL_Report ZL_CCtx_selectStartingGraphID(
        ZL_CCtx* cctx,
        const ZL_Compressor* compressor,
        ZL_GraphID graphID,
        const ZL_GraphParameters* rgp);

/**
 * @}
 */

/**
 * @}
 */

/**
 * @brief While it's possible to add elements (graphs, selectors or nodes) to a
 * Compressor one by one, and then finalize it by selecting a starting graph ID,
 * it's generally common for all these steps to be regrouped into a single
 * initialization function.
 * The following signature corresponds such a function.
 * It returns a GraphID which, by convention, is the starting GraphID.
 */
typedef ZL_GraphID (*ZL_GraphFn)(ZL_Compressor* compressor)
        ZL_NOEXCEPT_FUNC_PTR;

/**
 * @brief Initialize a @p compressor object with a `ZL_GraphFn` Graph function
 * @p f. It will register a few custom graphs and custom nodes, and set the
 * starting Graph ID. This is a convenience function, which is equivalent to
 * calling the Graph function, then ZL_Compressor_selectStartingGraphID()
 * followed by ZL_Compressor_validate()
 * @returns Success or an error which can be checked with ZL_isError().
 * @param f The function used to build the `ZL_GraphID`.
 */
ZL_Report ZL_Compressor_initUsingGraphFn(
        ZL_Compressor* compressor,
        ZL_GraphFn f);

// ZL_compress_usingCompressor()
// Simplified variant, without a ZL_CCtx* object to manage.
// Can be simpler to invoke, but note that :
// - a ZL_CCtx* object is created, initialized and freed every time
// - Global parameters can only be set via @compressor.
ZL_Report ZL_compress_usingCompressor(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        const ZL_Compressor* compressor);

/* In the following workflow,
 * the custom graph is defined solely with a function.
 * This makes it possible to pass a custom graph without a ZL_Compressor*
 * object, and therefore without the need to manage its lifetime.
 *
 * This makes it possible to compress without declaring nor managing
 * a compression state nor a custom graph state.
 * It makes ZL_compress_usingGraphFn() stateless,
 * which is a great way to simplify invocation, but
 * note that these objects will still be created, initialized and freed
 * within the function, so there is a performance cost associated.
 *
 * ZL_GraphFn is a function signature that defines a graph,
 * optionally set global parameters,
 * and returns the starting GraphID.
 * It doesn't have to call ZL_Compressor_selectStartingGraphID()
 * internally, this will be done automatically, using the @return
 * value.
 */

/**
 * @brief compresses using @param graphFunction that both defines a custom graph
 * and sets global parameters.
 */
ZL_Report ZL_compress_usingGraphFn(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphFunction);

#if defined(__cplusplus)
} // extern "C"
#endif
#endif // ZSTRONG_ZS_CGRAPH_API_H
