// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_segmenter.h"
#include "custom_parsers/csv/csv_lexer.h"
#include "custom_parsers/csv/csv_parser.h"
#include "openzl/common/assertion.h"

// Parameter to set the chunk size desired during chunking
#define ZL_CSV_CHUNK_BYTE_SIZE_MAX_ID 100
// Parameters for ZL_CsvParser_registerGraph:
// Set to 1 if the first line is a header line, 0 otherwise
#define ZL_PARSER_HAS_HEADER_PID 225
// The character separator between columns. e.g. `,` for comma `|` for pipe
#define ZL_PARSER_SEPARATOR_PID 226
// Whether to use the null-aware parser (1) or not (0)
#define ZL_PARSER_USE_NULL_AWARE_PID 227

static ZL_Report SEGM_csvProcessChunk(
        ZL_Segmenter* sctx,
        const ZL_CSV_lexResult* lexed,
        ZL_GraphID headGraph,
        size_t currentChunkByteSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    ZL_CopyParam cParam = {
        .paramId   = ZL_CSV_CHUNKED_LEXED_RESULT_ID,
        .paramPtr  = lexed,
        .paramSize = sizeof(ZL_CSV_lexResult),
    };
    ZL_LocalParams chunkParams = { .copyParams = { .copyParams   = &cParam,
                                                   .nbCopyParams = 1 } };
    const ZL_RuntimeGraphParameters gparams = {
        .localParams = &chunkParams,
    };
    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
            sctx, &currentChunkByteSize, 1, headGraph, &gparams));
    return ZL_returnSuccess();
}

static ZL_Report SEGM_csv(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    size_t const numInputs = ZL_Segmenter_numInputs(sctx);
    ZL_ERR_IF_NE(numInputs, 1, node_invalid_input);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ASSERT_NN(input);
    ZL_ERR_IF_NE(ZL_Input_type(input), ZL_Type_serial, node_invalid_input);
    const size_t byteSize     = ZL_Input_contentSize(input);
    const char* const content = (const char* const)ZL_Input_ptr(input);

    ZL_GraphIDList const customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ASSERT_EQ(customGraphs.nbGraphIDs, 1);
    ZL_GraphID const headGraph = customGraphs.graphids[0];
    // Note: assumes a positive chunkByteSizeMax is passed in.
    size_t chunkByteSizeMax = (size_t)ZL_Segmenter_getLocalIntParam(
                                      sctx, ZL_CSV_CHUNK_BYTE_SIZE_MAX_ID)
                                      .paramValue;
    int hasHeader =
            ZL_Segmenter_getLocalIntParam(sctx, ZL_PARSER_HAS_HEADER_PID)
                    .paramValue;
    int intSep = ZL_Segmenter_getLocalIntParam(sctx, ZL_PARSER_SEPARATOR_PID)
                         .paramValue;
    ZL_ERR_IF(
            (intSep > 255) || (intSep < 0),
            node_invalid_input,
            "Separator must be a char value");
    char sep = (char)intSep;
    int useNullAwareParse =
            ZL_Segmenter_getLocalIntParam(sctx, ZL_PARSER_USE_NULL_AWARE_PID)
                    .paramValue;
    ZL_ERR_IF(
            (useNullAwareParse != 0) && (useNullAwareParse != 1),
            node_invalid_input,
            "UseNullAware must be 0 or 1");

    ZL_CSV_lexResult lexed = {};
    ZL_Report lexRes       = (useNullAwareParse)
                  ? ZL_CSV_lexNullAware(
                      sctx, content, byteSize, hasHeader, sep, &lexed)
                  : ZL_CSV_lex(sctx, content, byteSize, hasHeader, sep, &lexed);
    ZL_ERR_IF_ERR(lexRes);

    // Can pick a subset of lexed result to pass in as local params
    size_t numElts     = ZL_Input_numElts(input);
    size_t prevIdx     = 0;
    size_t rowByteSize = 0;
    if (lexed.nbNewlines > 0) { // Can do chunking
        // Chunk must contain at least one row
        for (size_t start = 0; start <= lexed.newlineIndices[0]; start++) {
            rowByteSize += lexed.stringLens[start];
        }
        size_t currentChunkByteSize = rowByteSize;
        for (size_t row = 1; row < lexed.nbNewlines; row++) {
            rowByteSize = 0;
            for (size_t start = lexed.newlineIndices[row - 1] + 1;
                 start <= lexed.newlineIndices[row];
                 start++) {
                rowByteSize += lexed.stringLens[start];
            }
            if (currentChunkByteSize + rowByteSize > chunkByteSizeMax) {
                ZL_CSV_lexResult chunkedLex = {
                    .nbStrs     = lexed.newlineIndices[row - 1] - prevIdx + 1,
                    .nbColumns  = lexed.nbColumns,
                    .stringLens = lexed.stringLens + prevIdx,
                    .dispatchIndices = lexed.dispatchIndices + prevIdx,
                };
                ZL_ERR_IF_ERR(SEGM_csvProcessChunk(
                        sctx, &chunkedLex, headGraph, currentChunkByteSize));
                numElts -= currentChunkByteSize;
                currentChunkByteSize = 0;
                prevIdx              = lexed.newlineIndices[row - 1] + 1;
            }
            currentChunkByteSize += rowByteSize;
        }
    }
    ZL_CSV_lexResult chunkedLex = {
        .nbStrs          = lexed.nbStrs - prevIdx,
        .nbColumns       = lexed.nbColumns,
        .stringLens      = lexed.stringLens + prevIdx,
        .dispatchIndices = lexed.dispatchIndices + prevIdx,
    };
    ZL_ERR_IF_ERR(SEGM_csvProcessChunk(sctx, &chunkedLex, headGraph, numElts));
    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_GraphID)
ZL_CsvSegmenter_registerSegmenter(
        ZL_Compressor* compressor,
        size_t chunkByteSizeMax,
        bool hasHeader,
        char sep,
        bool useNullAware,
        const ZL_GraphID clusteringGraph)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);
    ZL_GraphID csvGraph =
            ZL_CsvParser_registerGraph(compressor, clusteringGraph);

    ZL_IntParam intParams[]  = { {
                                         .paramId    = ZL_PARSER_HAS_HEADER_PID,
                                         .paramValue = hasHeader,
                                },
                                 {
                                         .paramId    = ZL_PARSER_SEPARATOR_PID,
                                         .paramValue = sep,
                                },
                                 {
                                         .paramId = ZL_PARSER_USE_NULL_AWARE_PID,
                                         .paramValue = useNullAware,
                                },
                                 {
                                         .paramId =
                                                ZL_CSV_CHUNK_BYTE_SIZE_MAX_ID,
                                         .paramValue = (int)chunkByteSizeMax,
                                } };
    ZL_LocalParams csvParams = (ZL_LocalParams){
        .intParams = { .intParams = intParams, .nbIntParams = 4 },
    };
    ZL_Type inputType = ZL_Type_serial;
    ZL_GraphID segmenterBase =
            ZL_Compressor_getGraph(compressor, "CSV Segmenter");
    if (segmenterBase.gid == ZL_GRAPH_ILLEGAL.gid) {
        ZL_SegmenterDesc desc = {
            .name                = "!CSV Segmenter",
            .segmenterFn         = SEGM_csv,
            .inputTypeMasks      = &inputType,
            .numInputs           = 1,
            .lastInputIsVariable = false,
            .customGraphs        = NULL,
            .numCustomGraphs     = 0,
            .localParams         = {},
        };
        segmenterBase = ZL_Compressor_registerSegmenter(compressor, &desc);
    }
    ZL_ParameterizedGraphDesc const csvGraphDesc = {
        .graph          = segmenterBase,
        .localParams    = &csvParams,
        .customGraphs   = &csvGraph,
        .nbCustomGraphs = 1,
    };
    ZL_GraphID segmenter =
            ZL_Compressor_registerParameterizedGraph(compressor, &csvGraphDesc);
    ZL_ERR_IF_EQ(
            segmenter.gid,
            ZL_GRAPH_ILLEGAL.gid,
            GENERIC,
            "Graph parameterization failed");
    return ZL_RESULT_WRAP_VALUE(ZL_GraphID, segmenter);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_CsvSegmenter_registerSegmenterNoChunks(
        ZL_Compressor* compressor,
        bool hasHeader,
        char sep,
        bool useNullAware,
        const ZL_GraphID clusteringGraph)
{
    return ZL_CsvSegmenter_registerSegmenter(
            compressor,
            SIZE_MAX,
            hasHeader,
            sep,
            useNullAware,
            clusteringGraph);
}
