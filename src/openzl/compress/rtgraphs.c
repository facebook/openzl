// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/rtgraphs.h"
#include "openzl/common/allocation.h" // ZL_malloc
#include "openzl/common/assertion.h"  // ZS_ASSERT_*
#include "openzl/common/limits.h"
#include "openzl/common/logging.h" // ZL_DLOG
#include "openzl/common/stream.h"  // ZL_Data*, STREAM_getRBuffer
#include "openzl/common/vector.h"
#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

ZL_Report RTGM_init(RTGraph* rtgm)
{
    ZL_DLOG(OBJ + 1, "RTGM_init");
    ZL_ASSERT_NN(rtgm);
    ZL_ASSERT_NULL(rtgm->streamArena); // not initialized yet !
    rtgm->streamArena =
            ALLOC_HeapArena_create(); // using heap arena by default, can be
                                      // changed later with
                                      // RTGM_setStreamArenaType()
    ZL_RET_R_IF_NULL(allocation, rtgm->streamArena);
    rtgm->rtsidsArena = ALLOC_StackArena_create();
    ZL_RET_R_IF_NULL(allocation, rtgm->rtsidsArena);
    VECTOR_INIT(rtgm->nodes, ZL_runtimeNodeLimit(ZL_MAX_FORMAT_VERSION));
    VECTOR_INIT(rtgm->streams, ZL_runtimeStreamLimit(ZL_MAX_FORMAT_VERSION));
    rtgm->nextStreamUniqueID = 0;
    return ZL_returnSuccess();
}

static void RTGM_clearRTStreamsFrom(RTGraph* rtgraph, unsigned rank);
void RTGM_reset(RTGraph* rtgm)
{
    VECTOR_CLEAR(rtgm->nodes);
    RTGM_clearRTStreamsFrom(rtgm, 0);
    ALLOC_Arena_freeAll(rtgm->rtsidsArena);
    ALLOC_Arena_freeAll(rtgm->streamArena);
    rtgm->nextStreamUniqueID = 0;
    ZL_ASSERT_EQ(VECTOR_SIZE(rtgm->streams), 0);
}

void RTGM_destroy(RTGraph* rtgm)
{
    ZL_ASSERT_NN(rtgm);
    RTGM_reset(rtgm);
    VECTOR_DESTROY(rtgm->nodes);
    VECTOR_DESTROY(rtgm->streams);
    ALLOC_Arena_freeArena(rtgm->rtsidsArena);
    ALLOC_Arena_freeArena(rtgm->streamArena);
    rtgm->streamArena = NULL;
}

ZL_Report RTGM_setStreamArenaType(RTGraph* rtgm, ZL_DataArenaType sat)
{
    ZL_ASSERT_NN(rtgm);
    // Such modification should only be done when there is no stream,
    // i.e. between compression sessions
    ZL_ASSERT_EQ(VECTOR_SIZE(rtgm->streams), 0);
    Arena* newArena = NULL;
    switch (sat) {
        case ZL_DataArenaType_heap:
            newArena = ALLOC_HeapArena_create();
            break;
        case ZL_DataArenaType_stack:
            newArena = ALLOC_StackArena_create();
            break;
        default:
            ZL_RET_R_IF(parameter_invalid, 1, "Stream Arena type is invalid");
    }
    ZL_RET_R_IF_NULL(allocation, newArena);
    ALLOC_Arena_freeArena(rtgm->streamArena);
    rtgm->streamArena = newArena;
    return ZL_returnSuccess();
}

// RTGM_createNode():
// Create a RunTime Node, referencing a Node stored in CGraph.
// @return: ID of the RTNode created
// Condition: @nodeid must be correct (exist in CGraph)
// Note: this operation is presumed to NOT fail.
// It may fail in case of exhaustion in RTNode capacity,
// even if the manager is able to dynamically grow.
// But it's unclear if it's worth recovering from such a situation.
// For the time being, it's assumed that it's not.
ZL_RESULT_OF(RTNodeID)
RTGM_createNode(
        RTGraph* rtgraph,
        const CNode* cnode,
        const RTStreamID* inRtsids,
        size_t nbInRtsids)
{
    ZL_DLOG(SEQ, "RTGM_createNode (cnode: %s)", CNODE_getName(cnode));
    ZL_ASSERT_NN(rtgraph);
    size_t const nbOutSingletons = CNODE_getNbOut1s(cnode);

    // Assign new RTnode
    size_t const rtsids_byteSize = sizeof(RTStreamID) * nbInRtsids;
    ALLOC_ARENA_MALLOC_CHECKED_T(
            RTStreamID,
            rtsids_stored,
            nbInRtsids,
            rtgraph->rtsidsArena,
            RTNodeID);
    ZL_memcpy(rtsids_stored, inRtsids, rtsids_byteSize);
    RTNode node = {
        .cnode          = cnode,
        .inRtsids       = rtsids_stored,
        .nbInputs       = (ZL_IDType)nbInRtsids,
        .startOutRtsids = (ZL_IDType)VECTOR_SIZE(rtgraph->streams),
    };
    ZL_IDType const rtnodeid = (ZL_IDType)VECTOR_SIZE(rtgraph->nodes);
    // This allocation can fail if we ran into the limit
    // ZL_runtimeNodeLimit()
    ZL_RET_T_IF_NOT(
            RTNodeID,
            temporaryLibraryLimitation,
            VECTOR_PUSHBACK(rtgraph->nodes, node));

    // Reserve capacity to register out-streams
    // This allocation can fail if we ran into the limit
    // ZL_runtimeStreamLimit()
    size_t const newSize = VECTOR_SIZE(rtgraph->streams) + nbOutSingletons;
    ZL_RET_T_IF_NE(
            RTNodeID,
            temporaryLibraryLimitation,
            VECTOR_RESIZE(rtgraph->streams, newSize),
            newSize);

    return ZL_RESULT_WRAP_VALUE(RTNodeID, (RTNodeID){ rtnodeid });
}

size_t RTGM_getNbNodes(RTGraph const* rtnm)
{
    return VECTOR_SIZE(rtnm->nodes);
}

size_t RTGM_getNbStreams(RTGraph const* rtnm)
{
    return VECTOR_SIZE(rtnm->streams);
}

void RTGM_setNodeHeaderSegment(
        RTGraph* rtnm,
        RTNodeID rtnodeid,
        NodeHeaderSegment nhs)
{
    ZL_ASSERT_NN(rtnm);
    VECTOR_AT(rtnm->nodes, rtnodeid.rtnid).nodeHeaderSegment = nhs;
}

RTStreamID
RTGM_getOutStreamID(const RTGraph* rtnm, RTNodeID rtnodeid, int outIdx)
{
    ZL_IDType const rtnid = rtnodeid.rtnid;
    ZL_ASSERT_NN(rtnm);
    RTNode const* const node  = &VECTOR_AT(rtnm->nodes, rtnid);
    ZL_IDType const rtsid_pos = node->startOutRtsids + (ZL_IDType)outIdx;
    ZL_ASSERT_LT(rtsid_pos, VECTOR_SIZE(rtnm->streams));
    ZL_DLOG(BLOCK,
            "RTGM_getOutStreamID : rtnode=%u outidx=%u leads into rt_stream=%u",
            rtnid,
            outIdx,
            rtsid_pos);
    return (RTStreamID){ rtsid_pos };
}

// RTGM_getInStreamID():
// Condition : @rtnid must be valid, and @inIdx must be valid.
static RTStreamID
RTGM_getInStreamID(const RTGraph* rtnm, RTNodeID rtnodeid, int inIdx)
{
    ZL_IDType const rtnid = rtnodeid.rtnid;
    ZL_ASSERT_NN(rtnm);
    ZL_ASSERT_LT(rtnid, VECTOR_SIZE(rtnm->nodes));
    ZL_ASSERT_LT(inIdx, (int)VECTOR_AT(rtnm->nodes, rtnid).nbInputs);
    return VECTOR_AT(rtnm->nodes, rtnid).inRtsids[inIdx];
}

const CNode* RTGM_getCNode(const RTGraph* rtnm, RTNodeID rtnodeid)
{
    ZL_IDType const rtnid = rtnodeid.rtnid;
    return VECTOR_AT(rtnm->nodes, rtnid).cnode;
}

uint32_t
RTGM_getInputDistance(const RTGraph* rtnm, RTNodeID rtnodeid, int inIdx)
{
    ZL_ASSERT_NN(rtnm);
    ZL_IDType const rtnid = rtnodeid.rtnid;
    ZL_DLOG(BLOCK, "RTGM_getInputDistance (rtnode=%u, idx=%i)", rtnid, inIdx);
    ZL_ASSERT_LT(rtnid, VECTOR_SIZE(rtnm->nodes));
    ZL_IDType const inSid = RTGM_getInStreamID(rtnm, rtnodeid, inIdx).rtsid;
    RTNode const* node    = &VECTOR_AT(rtnm->nodes, rtnid);
    ZL_IDType const firstOutSid = node->startOutRtsids;
    ZL_ASSERT_GT(firstOutSid, inSid);
    return (uint32_t)(firstOutSid - inSid);
}

size_t RTGM_getNbInStreams(const RTGraph* rtnm, RTNodeID rtnodeid)
{
    ZL_ASSERT_NN(rtnm);
    ZL_IDType const rtnid = rtnodeid.rtnid;
    ZL_ASSERT_LT(rtnid, VECTOR_SIZE(rtnm->nodes));
    return VECTOR_AT(rtnm->nodes, rtnodeid.rtnid).nbInputs;
}

size_t RTGM_getNbOutStreams(const RTGraph* rtnm, RTNodeID rtnodeid)
{
    ZL_ASSERT_NN(rtnm);
    ZL_IDType const rtnid = rtnodeid.rtnid;
    ZL_ASSERT_LT(rtnid, VECTOR_SIZE(rtnm->nodes));
    return VECTOR_AT(rtnm->nodes, rtnid).nbOutStreams;
}

NodeHeaderSegment RTGM_nodeHeaderSegment(const RTGraph* rtnm, RTNodeID rtnodeid)
{
    ZL_ASSERT_NN(rtnm);
    ZL_IDType const rtnid = rtnodeid.rtnid;
    return VECTOR_AT(rtnm->nodes, rtnid).nodeHeaderSegment;
}

static ZL_DataID RTGM_genStreamID(RTGraph* rtgraph)
{
    return (ZL_DataID){ rtgraph->nextStreamUniqueID++ };
}

ZL_RESULT_OF(RTStreamID)
RTGM_addStream(
        RTGraph* rtgraph,
        RTNodeID rtnodeid,
        int outcomeID,
        int isVO,
        ZL_Type streamtype,
        size_t eltWidth,
        size_t eltsCapacity)
{
    ZL_DLOG(BLOCK, "RTGM_addStream (outcomeID=%i)", outcomeID);
    ZL_ASSERT_NN(rtgraph);
    RTNode* const rtnode = &VECTOR_AT(rtgraph->nodes, rtnodeid.rtnid);
    ZL_IDType rtsid;
    if (!isVO) {
        // Singleton output
        // space for Singleton is presumed already reserved
        ZL_ASSERT_NN(rtnode);
        ZL_RET_T_IF_GE(
                RTStreamID,
                successor_invalid,
                rtnode->startOutRtsids + (ZL_IDType)outcomeID,
                VECTOR_SIZE(rtgraph->streams),
                "attempted to provide an invalid Successor");
        rtsid = rtnode->startOutRtsids + (ZL_IDType)outcomeID;
    } else { // (isVO)
        // Variable output
        // Add one output to the Graph, after the reserved Singletons
        // Note : requires serialized stream creation (no concurrency)
        ZL_DLOG(SEQ, "adding a VO Stream");
        rtsid = (ZL_IDType)VECTOR_SIZE(rtgraph->streams);
        ZL_RET_T_IF(
                RTStreamID,
                allocation,
                VECTOR_RESIZE(rtgraph->streams, rtsid + 1) <= rtsid);
    }

    ZL_DLOG(SEQ, "new RT_stream at ID : %u", rtsid);
    RT_CStream* const rtStream = &VECTOR_AT(rtgraph->streams, rtsid);
    ZL_RET_T_IF_NN(
            RTStreamID,
            streamParameter_invalid,
            rtStream->stream,
            "this stream ID is already in use");

    ZL_Data* const stream = STREAM_createInArena(
            rtgraph->streamArena, RTGM_genStreamID(rtgraph));
    ZL_RET_T_IF_NULL(RTStreamID, allocation, stream, "Failed creating stream");

    ZL_Report const report =
            STREAM_reserve(stream, streamtype, eltWidth, eltsCapacity);
    if (ZL_isError(report)) {
        STREAM_free(stream);
        ZL_RET_T_IF_ERR(RTStreamID, report);
    }

    rtStream->stream = stream;
    ZL_ASSERT_GE(outcomeID, 0);
    rtStream->outcomeID = (ZL_IDType)outcomeID;
    rtnode->nbOutStreams++;
    return ZL_RESULT_WRAP_VALUE(RTStreamID, (RTStreamID){ rtsid });
}

// maps Input to internal Stream
ZL_RESULT_OF(RTStreamID)
RTGM_refInput(RTGraph* rtgraph, const ZL_Data* stream)
{
    ZL_DLOG(SEQ,
            "RTGM_refInput (id:%zu, size:%zu)",
            VECTOR_SIZE(rtgraph->streams),
            ZL_Data_contentSize(stream));
    RT_CStream rtstream = { .stream = STREAM_createInArena(
                                    rtgraph->streamArena,
                                    RTGM_genStreamID(rtgraph)) };
    ZL_RET_T_IF_NULL(RTStreamID, allocation, rtstream.stream);
    ZL_RET_T_IF_ERR(
            RTStreamID,
            STREAM_refStreamWithoutRefCount(rtstream.stream, stream));
    ZL_RET_T_IF_NOT(
            RTStreamID,
            allocation,
            VECTOR_PUSHBACK(rtgraph->streams, rtstream));
    return ZL_RESULT_WRAP_VALUE(
            RTStreamID,
            (RTStreamID){ (ZL_IDType)(VECTOR_SIZE(rtgraph->streams) - 1) });
}

// Note : this method is very similar to RTGM_addStream
// It mostly differs in what it does when it's successful,
// aka STREAM_reference() vs STREAM_reserve().
// But STREAM_reserve() can fail, while STREAM_reference() doesn't,
// which changes the return pattern.
// Nonetheless, there might be ways to share code between the 2 methods,
// since they share so much in common.
ZL_RESULT_OF(RTStreamID)
RTGM_refContentIntoNewStream(
        RTGraph* rtgraph,
        RTNodeID rtnodeid,
        int outcomeID,
        int isVO,
        ZL_Type streamtype,
        size_t eltWidth,
        size_t nbElts,
        ZL_Data const* src,
        size_t offsetBytes)
{
    ZL_DLOG(BLOCK, "RTGM_refContentIntoNewStream");
    ZL_ASSERT_NN(rtgraph);
    RTNode* const rtnode = &VECTOR_AT(rtgraph->nodes, rtnodeid.rtnid);
    ZL_IDType rtsid;
    if (!isVO) {
        // Singleton output
        // should be already reserved
        ZL_ASSERT_NN(rtnode);
        ZL_RET_T_IF_GE(
                RTStreamID,
                successor_invalid,
                rtnode->startOutRtsids + (ZL_IDType)outcomeID,
                VECTOR_SIZE(rtgraph->streams),
                "attempted to provide an invalid Successor");
        rtsid = rtnode->startOutRtsids + (ZL_IDType)outcomeID;
    } else { // isVO
        // Variable output
        // Add one output to the Graph, after the pre-reserved Singletons
        // Note : requires serialized stream creation (no concurrency)
        ZL_DLOG(SEQ, "adding a VO Stream");
        rtsid = (ZL_IDType)VECTOR_SIZE(rtgraph->streams);
        ZL_RET_T_IF(
                RTStreamID,
                allocation,
                VECTOR_RESIZE(rtgraph->streams, rtsid + 1) <= rtsid);
    }

    ZL_Data* const stream = STREAM_createInArena(
            rtgraph->streamArena, RTGM_genStreamID(rtgraph));
    ZL_RET_T_IF_NULL(RTStreamID, allocation, stream, "Failed creating stream");
    ZL_Report err = STREAM_refStreamByteSlice(
            stream, src, streamtype, offsetBytes, eltWidth, nbElts);
    if (ZL_isError(err)) {
        STREAM_free(stream);
        ZL_RET_T_IF_ERR(RTStreamID, err);
    }

    RT_CStream* const rtStream = &VECTOR_AT(rtgraph->streams, rtsid);
    ZL_ASSERT_NULL(rtStream->stream); // should be empty
    rtStream->stream = stream;
    ZL_ASSERT_GE(outcomeID, 0);
    rtStream->outcomeID = (ZL_IDType)outcomeID;
    rtnode->nbOutStreams++;
    return ZL_RESULT_WRAP_VALUE(RTStreamID, (RTStreamID){ rtsid });
}

void RTGM_storeStream(RTGraph* rtgraph, RTStreamID rtstreamid)
{
    ZL_IDType const rtsid = rtstreamid.rtsid;
    ZL_DLOG(BLOCK, "RTGM_storeStream id:%u", rtsid);
    ZL_ASSERT_NN(rtgraph);
    VECTOR_AT(rtgraph->streams, rtsid).toStore = 1;
}

// ****************    Accessors    *******************

ZL_WBuffer RTGM_getWBuffer(RTGraph* rtgraph, RTStreamID rtstream)
{
    ZL_ASSERT_NN(rtgraph);
    ZL_IDType const rtsid = rtstream.rtsid;
    return STREAM_getWBuffer(VECTOR_AT(rtgraph->streams, rtsid).stream);
}

ZL_RBuffer RTGM_getRBuffer(const RTGraph* rtgraph, RTStreamID rtstreamid)
{
    ZL_ASSERT_NN(rtgraph);
    ZL_IDType const rtsid = rtstreamid.rtsid;
    ZL_DLOG(BLOCK, "RTGM_getRBuffer from rtstreamID=%u", rtsid);

    return STREAM_getRBuffer(VECTOR_AT(rtgraph->streams, rtsid).stream);
}

const ZL_Data* RTGM_getRStream(const RTGraph* rtgraph, RTStreamID rtstreamid)
{
    ZL_DLOG(SEQ, "RTGM_getRStream (streamid==%u)", rtstreamid.rtsid);
    ZL_ASSERT_NN(rtgraph);
    ZL_IDType const rtsid = rtstreamid.rtsid;
    ZL_ASSERT_LT(rtsid, VECTOR_SIZE(rtgraph->streams));
    const ZL_Data* const stream = VECTOR_AT(rtgraph->streams, rtsid).stream;
    ZL_ASSERT_NN(stream); // should be valid
    return stream;
}

/* same as RTGM_getRStream(),
 * but requires the state to be writable in order to receive the reference */
ZL_Data* RTGM_getWStream(RTGraph* rtgraph, RTStreamID rtstreamid)
{
    ZL_DLOG(SEQ, "RTGM_getWStream (streamid==%u)", rtstreamid.rtsid);
    ZL_ASSERT_NN(rtgraph);
    ZL_IDType const rtsid = rtstreamid.rtsid;
    ZL_Data* const stream = VECTOR_AT(rtgraph->streams, rtsid).stream;

    if (stream == NULL) {
        ZL_DLOG(ERROR, "streamID=%u is invalid", rtsid);
    }
    ZL_ASSERT_NN(stream); // should be valid
    return stream;
}

ZL_IDType RTGM_getOutcomeID_fromRtstream(
        const RTGraph* rtgraph,
        RTStreamID rtstream)
{
    ZL_IDType const rtsid = rtstream.rtsid;
    ZL_DLOG(BLOCK, "RTGM_getOutcomeID_fromRtstream (rtsid = %u)", rtsid);
    ZL_ASSERT_NN(rtgraph);
    return VECTOR_AT(rtgraph->streams, rtsid).outcomeID;
}

ZL_Report RTGM_listBuffersToStore(
        const RTGraph* rtgraph,
        ZL_RBuffer* rba,
        size_t rbaCapacity)
{
    ZL_ASSERT_NN(rtgraph);
    ZL_ASSERT_GE(rbaCapacity, 1);
    ZL_ASSERT_NN(rba);
    ZL_ASSERT_GT(
            VECTOR_SIZE(rtgraph->streams), 0); // at least one stream produced
    // Scan all produced buffers from end to beginning,
    // report them into rba
    size_t nbBuffToStore     = 0;
    unsigned const nbStreams = (unsigned)VECTOR_SIZE(rtgraph->streams);
    for (unsigned n = 0; n < nbStreams; n++) {
        ZL_IDType const sid      = nbStreams - 1 - n;
        RT_CStream const* stream = &VECTOR_AT(rtgraph->streams, sid);
        if (stream->toStore) {
            ZL_ASSERT_NN(stream->stream);
            ZL_ASSERT_LT(nbBuffToStore, rbaCapacity);
            rba[nbBuffToStore++] = STREAM_getRBuffer(stream->stream);
        }
    }
    return ZL_returnValue(nbBuffToStore);
}

void RTGM_guardRTStream(
        RTGraph* rtgraph,
        RTStreamID rtstream,
        unsigned protectRank)
{
    ZL_DLOG(SEQ,
            "RTGM_guardRTStream (rtstream=%u, protectRank=%u)",
            rtstream.rtsid,
            protectRank);
    if (protectRank == 0)
        return;
    ZL_IDType const rtsid = rtstream.rtsid;
    ZL_ASSERT_NN(rtgraph);
    ZL_ASSERT_LT(rtsid, VECTOR_SIZE(rtgraph->streams));
    RT_CStream* rtcs = &VECTOR_AT(rtgraph->streams, rtsid);
    ZL_ASSERT(rtcs->stream == RTGM_getWStream(rtgraph, rtstream));
    ZL_ASSERT_EQ(rtcs->toStore, 0);

    if (rtcs->protectRank == 0) {
        rtcs->protectRank = protectRank;
    } else {
        ZL_ASSERT_GT(protectRank, rtcs->protectRank);
    }
}

void RTGM_clearRTStream(
        RTGraph* rtgraph,
        RTStreamID rtstream,
        unsigned protectRank)
{
    ZL_DLOG(SEQ,
            "RTGM_clearRTStream (rtstream=%u, protectRank=%u)",
            rtstream.rtsid,
            protectRank);
    ZL_ASSERT_NN(rtgraph);
    ZL_IDType const rtsid  = rtstream.rtsid;
    RT_CStream* const rtcs = &VECTOR_AT(rtgraph->streams, rtsid);
    ZL_ASSERT(rtcs->stream == RTGM_getWStream(rtgraph, rtstream));

    if (rtcs->toStore)
        return;
    if (rtcs->protectRank - 1 /* intentional underflow */ < protectRank - 1) {
        return;
    }

    STREAM_free(rtcs->stream);
    rtcs->stream = NULL;
}

// Remove all buffers created after that rank id.
// WARNING ! Very dangerous operation (stateful)
// To be used _ONLY_ in specific circumstances
static void RTGM_clearRTStreamsFrom(RTGraph* rtgraph, unsigned rank)
{
    size_t const nbStreams = VECTOR_SIZE(rtgraph->streams);
    if (rank == nbStreams)
        return;
    ZL_ASSERT_LT(rank, nbStreams);
    for (size_t n = rank; n < nbStreams; n++) {
        ZL_Data* stream = VECTOR_AT(rtgraph->streams, n).stream;
        STREAM_free(stream);
    }
    RT_CStream* streamsPtr = VECTOR_DATA(rtgraph->streams);
    ZL_zeroes(streamsPtr + rank, (nbStreams - rank) * sizeof(*streamsPtr));
    const size_t newSize = VECTOR_RESIZE(rtgraph->streams, rank);
    ZL_ASSERT_EQ(newSize, rank, "Guaranteed to succeed");
}

void RTGM_clearNodesFrom(RTGraph* rtgraph, unsigned nodeRank)
{
    if (VECTOR_SIZE(rtgraph->nodes) == nodeRank)
        return; // nothing to do
    ZL_ASSERT_LT(nodeRank, VECTOR_SIZE(rtgraph->nodes));
    RTNode* node = &VECTOR_AT(rtgraph->nodes, nodeRank);
    RTGM_clearRTStreamsFrom(rtgraph, node->startOutRtsids);
    size_t const nbNodes = VECTOR_SIZE(rtgraph->nodes) - nodeRank;
    ZL_zeroes(node, sizeof(*node) * nbNodes);
    const size_t newSize = VECTOR_RESIZE(rtgraph->nodes, nodeRank);
    ZL_ASSERT_EQ(newSize, nodeRank, "Guaranteed to succeed");
}

size_t RTGM_streamMemory(RTGraph const* rtgraph)
{
    return ALLOC_Arena_memAllocated(rtgraph->streamArena);
}
