// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/pytorch_model_parser.h"

#include <string.h>

#include "custom_parsers/zip_lexer.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/estimate.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"

typedef enum {
    PytorchModelSuccessor_U8            = 0,
    PytorchModelSuccessor_F16           = 1,
    PytorchModelSuccessor_F32           = 2,
    PytorchModelSuccessor_F64           = 3,
    PytorchModelSuccessor_OtherFiles    = 4,
    PytorchModelSuccessor_Precompressed = 5,
    PytorchModelSuccessor_Metadata      = 6,
    PytorchModelSuccessor_NumSuccessors = 7,
} PytorchModelSuccessor;

static PytorchModelSuccessor selectSuccessor(const char* ptr, size_t size)
{
    const size_t width = ZL_guessFloatWidth(ptr, size);
    switch (width) {
        default:
            ZL_ASSERT_FAIL("unreachable");
            ZL_FALLTHROUGH;
        case 1:
            return PytorchModelSuccessor_U8;
        case 2:
            return PytorchModelSuccessor_F16;
        case 4:
            return PytorchModelSuccessor_F32;
        case 8:
            return PytorchModelSuccessor_F64;
    }
}

static bool
startsWithPrefix(const char* filename, size_t filenameSize, const char* prefix)
{
    return filenameSize >= strlen(prefix)
            && memcmp(filename, prefix, strlen(prefix)) == 0;
}

static bool hasDir(const char* filename, size_t filenameSize, const char* dir)
{
    for (;;) {
        if (startsWithPrefix(filename, filenameSize, dir)) {
            return true;
        }
        const char* ptr = memchr(filename, '/', filenameSize);
        if (ptr == NULL) {
            return false;
        }
        const size_t offset = (size_t)(ptr - filename) + 1;
        filename            = filename + offset;
        filenameSize -= offset;
    }
}

static bool isDataFile(const char* filename, size_t filenameSize)
{
    return hasDir(filename, filenameSize, "data/")
            || hasDir(filename, filenameSize, "xl_model_weights/");
}

static ZL_Report
pytorchModelDynGraph(ZL_Graph* gctx, ZL_Edge* sctxs[], size_t nbIns)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    // Allow interesting fuzzing with smaller inputs
    const size_t kMultiplier = 4;
#else
    const size_t kMultiplier = 1024;
#endif
    const size_t kMaxSegmentSize = 1024 * kMultiplier;

    ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
    ZL_Edge* sctx               = sctxs[0];
    ZL_Input const* const input = ZL_Edge_getData(sctx);
    const size_t inputSize      = ZL_Input_numElts(input);

    ZL_ZipLexer lexer;
    ZL_RET_R_IF_ERR(ZL_ZipLexer_init(&lexer, ZL_Input_ptr(input), inputSize));

    const size_t nbFiles = ZL_ZipLexer_numFiles(&lexer);
    const size_t maxNbSegments =
            nbFiles * 4 + 2 + (inputSize / kMaxSegmentSize);

    size_t* const segmentSizes =
            ZL_Graph_getScratchSpace(gctx, maxNbSegments * sizeof(size_t));
    unsigned* const tags =
            ZL_Graph_getScratchSpace(gctx, maxNbSegments * sizeof(unsigned));
    ZL_RET_R_IF_NULL(allocation, segmentSizes);
    ZL_RET_R_IF_NULL(allocation, tags);

    // Iterate over all the tokens in the Zip file, and fill out segmentSizes
    // and tags.
    size_t nbSegments = 0;
    while (!ZL_ZipLexer_finished(&lexer)) {
        ZL_ZipToken tokens[32];
        ZL_TRY_LET_R(nbTokens, ZL_ZipLexer_lex(&lexer, tokens, 32));
        for (size_t i = 0; i < nbTokens; ++i) {
            ZL_RET_R_IF_GE(corruption, nbSegments, maxNbSegments);
            const ZL_ZipToken token = tokens[i];

            // Assign the appropiate tag to the token.
            if (token.type == ZL_ZipTokenType_CompressedData) {
                if (token.compressionMethod != 0) {
                    tags[nbSegments] = PytorchModelSuccessor_Precompressed;
                } else if (isDataFile(token.filename, token.filenameSize)) {
                    tags[nbSegments] = selectSuccessor(token.ptr, token.size);
                } else {
                    tags[nbSegments] = PytorchModelSuccessor_OtherFiles;
                }
            } else {
                tags[nbSegments] = PytorchModelSuccessor_Metadata;
            }

            // Combine consecutive occurrences of the same tag.
            if (nbSegments > 0 && tags[nbSegments] == tags[nbSegments - 1]
                && segmentSizes[nbSegments - 1] < kMaxSegmentSize) {
                segmentSizes[nbSegments - 1] += token.size;
            } else {
                segmentSizes[nbSegments] = token.size;
                ++nbSegments;
            }

            // Split large files into smaller segments to optimize
            // (de)compression speed by improving memory locality.
            while (segmentSizes[nbSegments - 1] > kMaxSegmentSize) {
                ZL_RET_R_IF_GE(corruption, nbSegments, maxNbSegments);
                const size_t segmentSize     = segmentSizes[nbSegments - 1];
                segmentSizes[nbSegments - 1] = kMaxSegmentSize;
                segmentSizes[nbSegments]     = segmentSize - kMaxSegmentSize;
                tags[nbSegments]             = tags[nbSegments - 1];
                ++nbSegments;
            }
        }
    }

    // Split the input according to segmentSizes
    ZL_TRY_LET_T(
            ZL_EdgeList,
            streams,
            ZL_Edge_runSplitNode(sctx, segmentSizes, nbSegments));
    const ZL_GraphIDList graphs = ZL_Graph_getCustomGraphs(gctx);
    ZL_ASSERT_EQ(streams.nbStreams, nbSegments);

    // Set the destination for every segment
    for (size_t i = 0; i < streams.nbStreams; ++i) {
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
                streams.streams[i], graphs.graphids[tags[i]]));
    }

    return ZL_returnSuccess();
}

ZL_GraphID ZL_createGraph_pytorchModelCompressor(ZL_Compressor* cgraph)
{
    ZL_GraphID f16Graph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            ZL_NODE_BFLOAT16_DECONSTRUCT,
            ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_HUFFMAN));
    f16Graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE16, f16Graph);

    ZL_GraphID f32Graph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            ZL_NODE_FLOAT32_DECONSTRUCT,
            ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_HUFFMAN));
    f32Graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE32, f32Graph);

    const ZL_GraphID f64Graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_FIELD_LZ);

    ZL_GraphID graphs[PytorchModelSuccessor_NumSuccessors];
    graphs[PytorchModelSuccessor_U8]            = ZL_GRAPH_HUFFMAN;
    graphs[PytorchModelSuccessor_F16]           = f16Graph;
    graphs[PytorchModelSuccessor_F32]           = f32Graph;
    graphs[PytorchModelSuccessor_F64]           = f64Graph;
    graphs[PytorchModelSuccessor_OtherFiles]    = ZL_GRAPH_ZSTD;
    graphs[PytorchModelSuccessor_Precompressed] = ZL_GRAPH_STORE;
    graphs[PytorchModelSuccessor_Metadata]      = ZL_GRAPH_ZSTD;

    ZL_Type const inputTypeMask     = ZL_Type_serial;
    const ZL_FunctionGraphDesc desc = {
        .name                = "pytorch model compressor",
        .graph_f             = pytorchModelDynGraph,
        .inputTypeMasks      = &inputTypeMask,
        .nbInputs            = 1,
        .lastInputIsVariable = false,
        .customGraphs        = graphs,
        .nbCustomGraphs      = PytorchModelSuccessor_NumSuccessors,
    };
    return ZL_Compressor_registerFunctionGraph(cgraph, &desc);
}
