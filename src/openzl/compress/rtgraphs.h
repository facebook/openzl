// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_RTGRAPH_H
#define OPENZL_COMPRESS_RTGRAPH_H

#include "openzl/common/allocation.h" // ALLOC_Arena_*
#include "openzl/common/vector.h"
#include "openzl/compress/cnode.h" // CNode
#include "openzl/shared/portability.h"
#include "openzl/zl_buffer.h"       // ZL_RBuffer, ZL_WCursor
#include "openzl/zl_data.h"         // ZL_Data
#include "openzl/zl_errors.h"       // ZL_Report
#include "openzl/zl_opaque_types.h" // ZL_IDType

ZL_BEGIN_C_DECLS

/* =====   Types   ===== */

/* RTNodeID and RTStreamID
 * Strong types, to reduce risks of confusion between different ID types.
 * RTNodeID represents a Node effectively committed in the final graph.
 * RTStreamID tracks existence of streams, a link between 2 nodes,
 *            and their related resources (buffer, ...)
 **/
typedef struct {
    ZL_IDType rtnid;
} RTNodeID;
ZL_RESULT_DECLARE_TYPE(RTNodeID);
typedef struct {
    ZL_IDType rtsid;
} RTStreamID;
ZL_RESULT_DECLARE_TYPE(RTStreamID);

/* NodeHeaderSegment
 * slice where RTNode's private header is stored
 **/
typedef struct {
    size_t startPos;
    size_t len;
} NodeHeaderSegment;

// RTNode:
// Node selected and created at RunTime during graph traversal
typedef struct {
    const CNode* cnode; // Description of the Transform
                        // Note: mostly for connexion map + name

    const RTStreamID* inRtsids; // input RTStreamIDs
    size_t nbInputs;

    ZL_IDType startOutRtsids; // Index at which Output RTStreamIDs are stored
                              // (within @.dstRtsids)
    size_t nbOutStreams;
    NodeHeaderSegment
            nodeHeaderSegment; // slice where RTNode's private header is stored
} RTNode;

// RT_CStream:
// Stream effectively created at RunTime.
// Owns the associated buffer(s).
//
// Note: considered for later on :
// - metadata:
//   + standard: cardinality, max value, stats, etc.
//   + custom: free form Integer (done), flat buffers?
// - speculative thought: stream bundles (or flows?)
//   + may change some definition to state that
//     exchanges between nodes always consist of "stream bundles",
//     it's just that, frequently, the bundle has only one stream.
typedef struct {
    ZL_Data* stream;      // hosts content, type, buffers, references
    ZL_IDType outcomeID;  // hint to help select Successor
    int toStore;          // record final store operation
    unsigned protectRank; // protect rtstream from clearing request
} RT_CStream;

DECLARE_VECTOR_TYPE(RTNode)
DECLARE_VECTOR_TYPE(RT_CStream)

/* The RTGraph Manager consists of:
 * an array of nodes,
 *    which contains references to a ZL_GraphID and surrounding streams,
 * and an array of streams,
 *    which host ZL_Data
 *    and ID of the destination Graph.
 * Both arrays are empty at the beginning.
 **/
typedef struct {
    VECTOR(RTNode) nodes;
    VECTOR(RT_CStream) streams;
    Arena* rtsidsArena;
    Arena* streamArena;
    ZL_IDType nextStreamUniqueID;
} RTGraph;

ZL_Report RTGM_init(RTGraph* rtgm);
void RTGM_destroy(RTGraph* rtgm);

/**
 * reclaim memory space consumed by rt_streams and rt_nodes.
 * Always successful.
 */
void RTGM_reset(RTGraph* rtgraph);

/* Note: in contrast with most parameters,
 * setStreamArenaType() choice remains sticky
 * until updated again, or @rtgm end of life. */
ZL_Report RTGM_setStreamArenaType(RTGraph* rtgm, ZL_DataArenaType sat);

/* =====   Methods associated to RTNodes   ===== */

// RTGM_createNode():
// Create a RunTime Node based on a model Graph stored in CGraph.
// @cnode must be valid.
// Note : this operation is presumed to NOT fail.
// But it can fail, for example in case of exhaustion in RTNode capacity,
// even if the manager was able to dynamically grow its size.
// It's unclear if it's worth recovering from such an error.
// For the time being, it's assumed that it's not.
ZL_RESULT_OF(RTNodeID)
RTGM_createNode(
        RTGraph* rtgraph,
        const CNode* cnode,
        const RTStreamID* inRtsids,
        size_t nbInRtsids);

/// @returns the number nodes created
size_t RTGM_getNbNodes(RTGraph const* rtnm);

/// @returns the number streams created,
/// note this may be less than the number of streams stored!
size_t RTGM_getNbStreams(RTGraph const* rtnm);

/* RTGM_setNodeHeaderSegment() :
 * set the size of private header size for this node.
 * Note : presumed to not fail,
 *        this requires @rtnodeid to be correct
 */
void RTGM_setNodeHeaderSegment(
        RTGraph* rtnm,
        RTNodeID rtnodeid,
        NodeHeaderSegment nhs);

// RTGM_getCNode():
// From an already created RunTime Node,
// retrieve the transform description as a CNode* pointer.
// @rtnodeid _must_ be correct (previously created),
// in which case this function cannot fail.
const CNode* RTGM_getCNode(const RTGraph* rtnm, RTNodeID rtnid);

/* RTGM_getInputDistance() :
 * provides distance between input port @inIdx and first output port.
 */
uint32_t
RTGM_getInputDistance(const RTGraph* rtnm, RTNodeID rtnodeid, int inIdx);

/* RTGM_getNbInStreams() :
 * Returns the number of input streams.
 */
size_t RTGM_getNbInStreams(const RTGraph* rtnm, RTNodeID rtnodeid);

/* RTGM_getNbOutStreams() :
 * Returns the number of output streams.
 */
size_t RTGM_getNbOutStreams(const RTGraph* rtnm, RTNodeID rtnodeid);

/* RTGM_nodeHeaderSegment() :
 * private header for this node.
 * Note : presumed to not fail,
 *        this requires @rtnodeid to be correct
 */
NodeHeaderSegment RTGM_nodeHeaderSegment(
        const RTGraph* rtnm,
        RTNodeID rtnodeid);

// RTGM_getOutStreamID():
// Get Output Stream of a designated RTNode.
// Condition : both rtnid and outIdx must be valid
RTStreamID RTGM_getOutStreamID(const RTGraph* rtnm, RTNodeID rtnid, int outIdx);

// Remove all notes created after that rank id (expected max 1)
// WARNING ! Very dangerous operation (stateful)
// To be used _ONLY_ in specific circumstances
void RTGM_clearNodesFrom(RTGraph* rtgraph, unsigned nodeRank);

// Remove all streams created after that rank id
// WARNING ! Very dangerous operation (stateful)
// To be used _ONLY_ in specific circumstances
void RTGM_clearRTStreamsFrom(RTGraph* rtgraph, unsigned rank);

/* =====   Methods associated to RTStreams   ===== */

// RTGM_refInput() :
// Creates a first RTStream as a read-only reference to a typed ZL_Data.
// @return : the newly created RTStreamID
// Note 1 : This must be the first stream operation,
// before creating any other stream.
// Note 2 : Presuming pre-requisites are respected,
// this operation should always be successful.
ZL_RESULT_OF(RTStreamID)
RTGM_refInput(RTGraph* rtgraph, const ZL_Data* stream);

// RTGM_addStream() :
// A new Stream is added to the RunTime graph.
// This operation also allocates stream's associated buffer.
// @rtgraph : is the active RunTime Graph manager, being populated
// @rtnodeid : ID of the RT node to which a successor stream is added
// @streamtype : (note : could also be provided through cgraph+gsid)
// @eltsCapacity : Nb of elts to allocate (note: not nb of bytes!)
// @eltWidth : note : not all width are allowed, this depends on stream type
// @outcomeID : output ID from a CNode declaration order perspective
// @isVO : is the requested output VO or Singleton
// @return : the resulting RTStreamID created
//
// Note : This operation *can fail*.
// Notably, the manager could run out of space (no more stream possible)
// and/or buffer allocation could fail.
ZL_RESULT_OF(RTStreamID)
RTGM_addStream(
        RTGraph* rtgraph,
        RTNodeID rtnodeid,
        int outcomeID,
        int isVO,
        ZL_Type streamtype,
        size_t eltWidth,
        size_t eltCapacity);

/* RTGM_refContentIntoNewStream() :
 * reference slice in @ref starting at @offsetBytes
 * as a read-only content for @return RTStreamID.
 **/
ZL_RESULT_OF(RTStreamID)
RTGM_refContentIntoNewStream(
        RTGraph* rtgraph,
        RTNodeID rtnodeid,
        int outcomeID,
        int isVO,
        ZL_Type streamtype,
        size_t eltWidth,
        size_t eltCount,
        const ZL_Data* ref,
        size_t offsetBytes);

// RTGM_storeStream() :
// Tag the stream to be stored into final frame at collection stage.
// @rtsid must be valid
void RTGM_storeStream(RTGraph* rtgraph, RTStreamID rtsid);

// Note : rtsid **must** be valid,
//        meaning the stream exists,
//        and a buffer has already been allocated for it.
// in which case, this function cannot fail.
// @return a ZL_WBuffer structure, providing a writable area.
ZL_WBuffer RTGM_getWBuffer(RTGraph* rtgraph, RTStreamID rtsid);

// Note : rtsid **must** be valid.
// in which case, this function cannot fail.
// @return a read-only reference to relevant stream content buffer
ZL_RBuffer RTGM_getRBuffer(const RTGraph* rtgraph, RTStreamID rtsid);

// Note : rtsid **must** be valid.
// in which case, this function cannot fail.
// @return a read-only reference to relevant stream
const ZL_Data* RTGM_getRStream(const RTGraph* rtgraph, RTStreamID rtsid);

// Return writable reference to relevant stream
ZL_Data* RTGM_getWStream(RTGraph* rtgraph, RTStreamID rtsid);

// Note : rtsid **must** be valid.
ZL_IDType RTGM_getOutcomeID_fromRtstream(
        const RTGraph* rtgraph,
        RTStreamID rtsid);

ZL_Report RTGM_listBuffersToStore(
        const RTGraph* rtgraph,
        ZL_RBuffer* rba,
        size_t rbaCapacity);

/**
 * Protect @rtstream from clear request.
 * This is used by Graphs, to ensure that their Inputs are still available at
 * the end of Graph's execution, so that they can be redirected if need be.
 * @protectRank will serve as a barrier to refuse release requests without
 * proper rank. It's typically filled with Graph's depth (distance from root).
 * In this scheme, 1 has the highest rank, with higher values meaning lower
 * priority. 0 is a special value than means "unprotected".
 */
void RTGM_guardRTStream(
        RTGraph* rtgraph,
        RTStreamID rtstream,
        unsigned protectRank);

/**
 * Frees the ZL_Data for @p rtstream which means that it can no
 * longer be accessed.
 */
void RTGM_clearRTStream(
        RTGraph* rtgraph,
        RTStreamID rtstream,
        unsigned protectRank);

/**
 * @returns Current memory budget allocated for Stream content by the RTGraph.
 */
size_t RTGM_streamMemory(const RTGraph* rtgraph);

ZL_END_C_DECLS

#endif // OPENZL_COMPRESS_RTGRAPH_H
