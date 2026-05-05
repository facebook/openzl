// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_segmenter.h"
#include "custom_parsers/csv/csv_lexer.h"
#include "custom_parsers/csv/csv_parser.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_selector.h"

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
        ZL_GraphID headGraph,
        size_t chunkSizeBytes,
        const ZL_CSV_TokenType* types,
        const uint32_t* sizes,
        const uint32_t* cols,
        size_t numTokens,
        size_t numCols,
        bool hasHeader)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    const ZL_IntParam intParams[2] = {
        {
                ZL_CSV_CHUNKED_HAS_HEADER_ID,
                (int)hasHeader,
        },
        {
                ZL_CSV_CHUNKED_NUM_COLS_ID,
                (int)numCols,
        },
    };
    const ZL_RefParam refParams[3] = {
        {
                ZL_CSV_CHUNKED_TYPES_ID,
                types,
                numTokens * sizeof(types[0]),
        },
        {
                ZL_CSV_CHUNKED_SIZES_ID,
                sizes,
                numTokens * sizeof(sizes[0]),
        },
        {
                ZL_CSV_CHUNKED_COLS_ID,
                cols,
                numTokens * sizeof(cols[0]),
        },
    };
    const ZL_LocalParams chunkParams = {
        .intParams = { intParams, 2 },
        .refParams = { refParams, 3 },
    };
    const ZL_RuntimeGraphParameters gparams = {
        .localParams = &chunkParams,
    };
    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
            sctx, &chunkSizeBytes, 1, headGraph, &gparams));
    return ZL_returnSuccess();
}

static ZL_Report tryGetIntParam(ZL_Segmenter* segmenter, int key)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(segmenter);
    ZL_IntParam intParam = ZL_Segmenter_getLocalIntParam(segmenter, key);
    ZL_ERR_IF_EQ(intParam.paramId, ZL_LP_INVALID_PARAMID, node_invalid_input);
    return ZL_returnValue((size_t)intParam.paramValue);
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

    if (byteSize == 0) {
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &byteSize, 1, ZL_GRAPH_STORE, NULL));
        return ZL_returnSuccess();
    }

    ZL_GraphIDList const customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ASSERT_EQ(customGraphs.nbGraphIDs, 1);
    ZL_GraphID const headGraph = customGraphs.graphids[0];
    // Note: assumes a positive chunkByteSizeMax is passed in.
    ZL_TRY_LET_CONST(
            size_t,
            chunkByteSizeMax,
            tryGetIntParam(sctx, ZL_CSV_CHUNK_BYTE_SIZE_MAX_ID));
    ZL_TRY_LET(
            size_t, hasHeader, tryGetIntParam(sctx, ZL_PARSER_HAS_HEADER_PID));
    ZL_TRY_LET_CONST(
            size_t, intSep, tryGetIntParam(sctx, ZL_PARSER_SEPARATOR_PID));
    ZL_ERR_IF(
            intSep > 127, node_invalid_input, "Separator must be a char value");
    char sep = (char)intSep;
    ZL_TRY_LET_CONST(
            size_t,
            useNullAwareParse,
            tryGetIntParam(sctx, ZL_PARSER_USE_NULL_AWARE_PID));

    const size_t chunkSize = ZL_MIN(byteSize, chunkByteSizeMax);
    ZL_ERR_IF_GE(chunkSize, 1u << 30, node_invalid_input, "chunk size too big");
    // Each byte can produce up to 2 tokens (empty field + separator/newline).
    const size_t maxNumTokens = 2 * chunkSize + 1;
    ZL_CSV_TokenType* types   = ZL_Segmenter_getScratchSpace(
            sctx, maxNumTokens * sizeof(ZL_CSV_TokenType));
    ZL_ERR_IF_NULL(types, allocation);
    uint32_t* sizes =
            ZL_Segmenter_getScratchSpace(sctx, maxNumTokens * sizeof(uint32_t));
    ZL_ERR_IF_NULL(sizes, allocation);
    uint32_t* cols =
            ZL_Segmenter_getScratchSpace(sctx, maxNumTokens * sizeof(uint32_t));
    ZL_ERR_IF_NULL(cols, allocation);

    ZL_CSV_Lexer lexer;
    ZL_CSV_Lexer_init(
            &lexer,
            ZL_Segmenter_getOperationContext(sctx),
            content,
            byteSize,
            sep,
            useNullAwareParse);

    while (!ZL_CSV_Lexer_finished(&lexer)) {
        const char* const begin = lexer.src;
        ZL_TRY_LET_CONST(
                size_t,
                numTokens,
                ZL_CSV_Lexer_lex(
                        &lexer,
                        types,
                        sizes,
                        cols,
                        maxNumTokens,
                        chunkByteSizeMax));
        ZL_ERR_IF_EQ(
                lexer.row,
                0,
                node_invalid_input,
                "CSV is not well formed: No newline found");
        const char* const end = lexer.src;
        ZL_ERR_IF_EQ(numTokens, 0, logicError, "CSV lexer produced no tokens");
        if (end != lexer.end) {
            ZL_ERR_IF_LT(
                    (size_t)(end - begin),
                    chunkByteSizeMax,
                    logicError,
                    "CSV chunk consumed fewer bytes than expected");
            ZL_ERR_IF_NE(
                    types[numTokens - 1],
                    ZL_CSV_TokenType_Newline,
                    logicError,
                    "CSV chunk does not end with a newline");
        }
        ZL_ERR_IF_ERR(SEGM_csvProcessChunk(
                sctx,
                headGraph,
                (size_t)(end - begin),
                types,
                sizes,
                cols,
                numTokens,
                lexer.numCols,
                hasHeader));
        // Subsequent chunks have no header
        hasHeader = false;
    }
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
