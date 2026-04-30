// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/errors_internal.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_opaque_types.h"

#include "openzl/codecs/tokenize/encode_tokenize_binding.h"
#include "openzl/codecs/tokenize/encode_tokenize_kernel.h"
#include "openzl/compress/enc_interface.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/pdqsort.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

#define ZL_TOKENIZE_TOKENIZER_PID 1

struct ZL_CustomTokenizeState_s {
    ZL_Encoder* eictx;
    void const* opaque;
    ZL_Input const* input;
};

void const* ZL_CustomTokenizeState_getOpaquePtr(
        ZL_CustomTokenizeState const* ctx)
{
    return ctx->opaque;
}

void* ZL_CustomTokenizeState_createAlphabetOutput(
        ZL_CustomTokenizeState* ctx,
        size_t alphabetSize)
{
    ZL_Output* stream = ZL_Encoder_createTypedStream(
            ctx->eictx, 0, alphabetSize, ZL_Input_eltWidth(ctx->input));
    if (stream == NULL) {
        return NULL;
    }
    void* r = ZL_Output_ptr(stream);
    if (ZL_isError(ZL_Output_commit(stream, alphabetSize))) {
        return NULL;
    }
    return r;
}

void* ZL_CustomTokenizeState_createIndexOutput(
        ZL_CustomTokenizeState* ctx,
        size_t indexWidth)
{
    size_t const indexSize = ZL_Input_numElts(ctx->input);
    ZL_Output* stream =
            ZL_Encoder_createTypedStream(ctx->eictx, 1, indexSize, indexWidth);
    if (stream == NULL) {
        return NULL;
    }
    void* r = ZL_Output_ptr(stream);
    if (ZL_isError(ZL_Output_commit(stream, indexSize))) {
        return NULL;
    }
    return r;
}

typedef struct {
    ZL_CustomTokenizeFn customTokenizeFn;
    void const* opaque;
} ZL_CustomTokenize_Param;

// Tokenize uses only one map from uint64 -> size_t for simplicity.
// If we want to specialize for eltWidths, we could be a bit more efficient.
// For now we use the default hash function (xxh3) and equality functions.
// This provides a very strong hash function, but we may be able to sacrifice
// some hash quality for speed later on.
ZL_DECLARE_MAP_TYPE(Map8, uint64_t, size_t);

ZL_FORCE_INLINE uint64_t
readTokenAt(void const* data, size_t i, size_t eltWidth)
{
    ZL_ASSERT(ZL_isLittleEndian(), "Only works on little-endian currently");
    ZL_ASSERT_LE(eltWidth, 8);
    uint64_t token = 0;
    memcpy(&token, (uint8_t const*)data + i * eltWidth, eltWidth);
    return token;
}

ZL_FORCE_INLINE void
writeTokenAt(uint64_t token, void* data, size_t i, size_t eltWidth)
{
    ZL_ASSERT_LE(eltWidth, 8);
    memcpy((uint8_t*)data + i * eltWidth, &token, eltWidth);
}

ZL_FORCE_INLINE void
writeIndexAt(size_t index, void* data, size_t i, size_t idxWidth)
{
    ZL_ASSERT_LE(idxWidth, sizeof(size_t));
    memcpy((uint8_t*)data + i * idxWidth, &index, idxWidth);
}

// Inlined specialization to write indices templated both on eltWidth &
// idxWidth.
ZL_FORCE_INLINE ZL_Report writeIndicesImpl(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        Map8 const* alphabet,
        size_t eltWidth,
        size_t idxWidth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    void const* const input = ZL_Input_ptr(in);
    size_t const nbElts     = ZL_Input_numElts(in);

    ZL_Output* out = ZL_Encoder_createTypedStream(eictx, 1, nbElts, idxWidth);
    ZL_ERR_IF_NULL(out, allocation);
    void* const indices = ZL_Output_ptr(out);

    for (size_t i = 0; i < nbElts; ++i) {
        uint64_t const token = readTokenAt(input, i, eltWidth);
        size_t const index   = Map8_findVal(alphabet, token)->val;
        writeIndexAt(index, indices, i, idxWidth);
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));

    return ZL_returnSuccess();
}

ZL_FORCE_INLINE ZL_Report tokenizeImpl(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        Map8* tokToIdx,
        bool sort,
        size_t eltWidth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(eltWidth, ZL_Input_eltWidth(in));

    void const* const input = ZL_Input_ptr(in);
    size_t const nbElts     = ZL_Input_numElts(in);

    // Reserve up to 256 entries to skip past the small growth stage.
    if (!Map8_reserve(tokToIdx, ZL_MIN(256, (uint32_t)nbElts), false)) {
        ZL_ERR(allocation);
    }

    // Build the alphabet map.
    bool badAlloc  = false;
    size_t nextIdx = 0;
#if defined(__GNUC__) && defined(__x86_64__)
    __asm__(".p2align 6");
#endif
    for (size_t i = 0; i < nbElts; ++i) {
        uint64_t const token = readTokenAt(input, i, eltWidth);
        // Check for contains first because we expect many duplicates,
        // and contains is faster than insert when the key is present.
        if (!Map8_containsVal(tokToIdx, token)) {
            size_t const value = nextIdx++;
            Map8_Insert const insert =
                    Map8_insertVal(tokToIdx, (Map8_Entry){ token, value });
            ZL_ASSERT(insert.badAlloc || insert.inserted);
            // Batch up any allocation failures at the end, so we don't have
            // early exits from this loop.
            badAlloc |= insert.badAlloc;
        }
    }

    ZL_ERR_IF(badAlloc, allocation);

    size_t const alphabetSize = Map8_size(tokToIdx);

    ZL_Output* out =
            ZL_Encoder_createTypedStream(eictx, 0, alphabetSize, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    // Write the alphabet
    // TODO(terrelln): Right now we have to write the alphabet out after the
    // first loop because we have no way to resize output streams, and we
    // don't know the size of the output until we finish the loop.
    void* const alphabet = ZL_Output_ptr(out);
    {
        Map8_IterMut iter = Map8_iterMut(tokToIdx);
        for (Map8_Entry* entry; (entry = Map8_IterMut_next(&iter));) {
            writeTokenAt(entry->key, alphabet, entry->val, eltWidth);
        }
    }

    // Sort the alphabet if needed
    if (sort) {
        pdqsort(alphabet, alphabetSize, eltWidth);
        for (size_t i = 0; i < alphabetSize; ++i) {
            uint64_t const token = readTokenAt(alphabet, i, eltWidth);
            Map8_Entry* entry    = Map8_findMutVal(tokToIdx, token);
            ZL_ASSERT_NN(entry);
            entry->val = i;
        }
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(out, alphabetSize));

    ZL_ERR_IF_GT(
            alphabetSize,
            UINT32_MAX,
            temporaryLibraryLimitation,
            "Only 4 byte indices supported... But why do you want this?");

    // Write the indices in the smallest output possible
    if (alphabetSize <= (1u << 8)) {
        return writeIndicesImpl(eictx, in, tokToIdx, eltWidth, 1);
    }
    if (eltWidth > 1 && alphabetSize <= (1u << 16)) {
        return writeIndicesImpl(eictx, in, tokToIdx, eltWidth, 2);
    }
    if (eltWidth > 2) {
        return writeIndicesImpl(eictx, in, tokToIdx, eltWidth, 4);
    }

    ZL_ERR(logicError, "Impossible");
}

#define GEN_TOKENIZE(eltWidth)                                                \
    ZL_FORCE_NOINLINE ZL_Report tokenize##eltWidth(                           \
            ZL_Encoder* eictx, ZL_Input const* in, Map8* alphabet, bool sort) \
    {                                                                         \
        return tokenizeImpl(eictx, in, alphabet, sort, eltWidth);             \
    }

GEN_TOKENIZE(2)
GEN_TOKENIZE(4)
GEN_TOKENIZE(8)

#include "openzl/codecs/tokenize/encode_tokenize2to1_kernel.h" // TOK2_*

// tokenize2_shell() :
// route between the specialized TOK2_* implementation
// and the generic tokenize2 implementation
// if conditions are right
// i.e. mainly alphabetSize <= 256.
static ZL_Report tokenize2_shell(
        ZL_Encoder* eictx,
        ZL_Input const* in,
        Map8* alphabet,
        bool sort)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 2);

    if (!sort || ZL_Input_type(in) == ZL_Type_struct) {
        /* Currently, !sort actually means
         * "dictionary is generated using input occurrence order",
         * and not "no particular order".
         * Therefore, don't use TOK2_*, which employs native numeric order.
         * In the case of ZL_Type_struct, `sort` actually means
         * alphabetical order,
         * which is different from the order produced by TOK2_fsf_* variant.
         * Reverting to generic tokenize2() for these cases.
         * Note : we probably need a better defined approach to "dictionary
         * order". In particular, we should spell out "input occurrence
         * order" more explicitly, and eventually create a separate "no
         * particular order" category.
         */
        return tokenize2(eictx, in, alphabet, sort);
    }

    /* Only case allowed : numeric input, numeric order */
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    const void* const srcPtr = ZL_Input_ptr(in);
    size_t const nbSymbols   = ZL_Input_numElts(in);

    if (nbSymbols < 5000) {
        /* TOK2_* is about x5 faster than tokenize2(),
         * but it features a fixed processing overhead
         * which becomes dominant for small inputs.
         * The 5000 cutoff value is an heuristic, discovered through
         * benchmark. In the future, this cutoff value could be updated with
         * optimizations.
         */
        return tokenize2(eictx, in, alphabet, sort);
    }

    void* const workspace =
            ZL_Encoder_getScratchSpace(eictx, TOK2_CARDINALITY_MAX);
    ZL_ERR_IF_NULL(workspace, allocation);

    size_t const alphabetSize =
            TOK2_numSort_cardinality(workspace, srcPtr, nbSymbols);

    if (alphabetSize > 256) {
        /* fast variant TOK2_numSort_encode_into1()
         * only works for small alphabets <= 256 */
        return tokenize2(eictx, in, alphabet, sort);
    }

    ZL_DLOG(SEQ, "tokenize2_shell: alphabetSize: %zu", alphabetSize);
    ZL_Output* const alphabetStream =
            ZL_Encoder_createTypedStream(eictx, 0, alphabetSize, 2);
    ZL_ERR_IF_NULL(alphabetStream, allocation);

    ZL_Output* const indexStream =
            ZL_Encoder_createTypedStream(eictx, 1, nbSymbols, 1);
    ZL_ERR_IF_NULL(indexStream, allocation);

    TOK2_numSort_encode_into1(
            ZL_Output_ptr(indexStream),
            nbSymbols,
            ZL_Output_ptr(alphabetStream),
            alphabetSize,
            srcPtr,
            nbSymbols,
            workspace);

    ZL_ERR_IF_ERR(ZL_Output_commit(alphabetStream, alphabetSize));
    ZL_ERR_IF_ERR(ZL_Output_commit(indexStream, nbSymbols));

    return ZL_returnSuccess();
}

ZL_FORCE_INLINE ZL_Report
tokenize1(ZL_Encoder* eictx, ZL_Input const* in, bool sort)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(1, ZL_Input_eltWidth(in));
    const uint8_t* input = (const uint8_t*)ZL_Input_ptr(in);
    const size_t nbElts  = ZL_Input_numElts(in);

    size_t alphabetSize;
    ZL_Output* alphabetStream = ZL_Encoder_createTypedStream(eictx, 0, 256, 1);
    ZL_ERR_IF_NULL(alphabetStream, allocation);
    uint8_t* alphabet = (uint8_t*)ZL_Output_ptr(alphabetStream);

    ZL_Output* indicesStream =
            ZL_Encoder_createTypedStream(eictx, 1, nbElts, 1);
    ZL_ERR_IF_NULL(indicesStream, allocation);
    uint8_t* indices = (uint8_t*)ZL_Output_ptr(indicesStream);

    if (sort) {
        uint8_t alphabetReverseMap[256] = { 0 };
        for (size_t i = 0; i < nbElts; ++i) {
            alphabetReverseMap[input[i]] = 1;
        }
        alphabetSize = 0;
        for (size_t i = 0; i < 256; ++i) {
            if (alphabetReverseMap[i]) {
                alphabetReverseMap[i]  = (uint8_t)alphabetSize;
                alphabet[alphabetSize] = (uint8_t)i;
                alphabetSize++;
            }
        }
        for (size_t i = 0; i < nbElts; ++i) {
            indices[i] = alphabetReverseMap[input[i]];
        }
    } else {
        alphabetSize = 0;
        int alphabetReverseMap[256];
        memset(alphabetReverseMap, -1, sizeof(int) * 256);

        for (size_t i = 0; i < nbElts; ++i) {
            if (alphabetReverseMap[input[i]] == -1) {
                alphabetReverseMap[input[i]] = (int)alphabetSize;
                indices[i]                   = (uint8_t)alphabetSize;
                alphabet[alphabetSize]       = input[i];
                alphabetSize++;
            } else {
                indices[i] = (uint8_t)alphabetReverseMap[input[i]];
            }
        }
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(alphabetStream, alphabetSize));
    ZL_ERR_IF_ERR(ZL_Output_commit(indicesStream, nbElts));
    return ZL_returnSuccess();
}

static ZL_Report
tokenize(ZL_Encoder* eictx, Map8* alphabet, const ZL_Input* in, bool sort)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(in);
    switch (ZL_Input_eltWidth(in)) {
        case 1:
            return tokenize1(eictx, in, sort);
        case 2:
            return tokenize2_shell(eictx, in, alphabet, sort);
        case 4:
            return tokenize4(eictx, in, alphabet, sort);
        case 8:
            return tokenize8(eictx, in, alphabet, sort);
        default:
            ZL_ERR(temporaryLibraryLimitation,
                   "Elt width %u not supported yet! But decoder the decoder "
                   "supports all width, so you only need to extend the encoder",
                   (unsigned)ZL_Input_eltWidth(in));
    }
}

static ZL_Report
EI_tokenizeImpl(ZL_Encoder* eictx, const ZL_Input* in, bool sort)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

    ZL_ERR_IF(
            ZL_Input_type(in) != ZL_Type_numeric && sort,
            graph_invalid,
            "sort only works on numeric inputs");

    size_t const nbElts    = ZL_Input_numElts(in);
    Map8 alphabet          = Map8_create((uint32_t)nbElts + 1);
    ZL_Report const report = tokenize(eictx, &alphabet, in, sort);
    Map8_destroy(&alphabet);
    ZL_ERR_IF_ERR(report);

    return ZL_returnValue(2);
}

static bool EI_tokenizeShouldSort(const ZL_Encoder* encoder)
{
    const ZL_IntParam param =
            ZL_Encoder_getLocalIntParam(encoder, ZL_TOKENIZE_SORT_PID);
    if (param.paramId == ZL_TOKENIZE_SORT_PID) {
        return param.paramValue != 0;
    } else {
        return false;
    }
}

ZL_Report EI_tokenize(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_CopyParam genericParam =
            ZL_Encoder_getLocalCopyParam(eictx, ZL_TOKENIZE_TOKENIZER_PID);
    if (genericParam.paramId == ZL_TOKENIZE_TOKENIZER_PID) {
        ZL_CustomTokenize_Param const param =
                *(ZL_CustomTokenize_Param const*)genericParam.paramPtr;
        ZL_CustomTokenizeState ctx = {
            .eictx  = eictx,
            .opaque = param.opaque,
            .input  = in,
        };
        ZL_ERR_IF_ERR((param.customTokenizeFn)(&ctx, in));
        return ZL_returnSuccess();
    }
    return EI_tokenizeImpl(eictx, in, EI_tokenizeShouldSort(eictx));
}

static size_t getMinIdxSpace(size_t const alphabetSize)
{
    if (alphabetSize <= (1u << 8)) {
        return 1;
    }
    if (alphabetSize <= (1u << 16)) {
        return 2;
    }
    return 4;
}

static ZL_Report EI_tokenizeVSFImpl(
        ZL_Encoder* eictx,
        MapVSF* tokToIdx,
        const ZL_Input* in,
        bool sort)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_string);

    const uint8_t* const src         = ZL_Input_ptr(in);
    size_t const nbElts              = ZL_Input_numElts(in);
    const uint32_t* const fieldSizes = ZL_Input_stringLens(in);

    // Build alphabet of input stream
    size_t alphabetFieldSizesSum;
    ZL_ERR_IF_ERR(ZS_buildTokenizeVsfAlphabet(
            tokToIdx, &alphabetFieldSizesSum, src, fieldSizes, nbElts));
    size_t const alphabetSize = MapVSF_size(tokToIdx);

    // Create alphabet stream
    ZL_Output* const alphabet =
            ZL_Encoder_createTypedStream(eictx, 0, alphabetFieldSizesSum, 1);
    ZL_ERR_IF_NULL(alphabet, allocation);

    uint32_t* const alphabetFieldSizes =
            ZL_Output_reserveStringLens(alphabet, alphabetSize);
    ZL_ERR_IF_NULL(alphabetFieldSizes, allocation);

    // Create indices stream
    size_t const idxWidth = getMinIdxSpace(alphabetSize);
    ZL_Output* const indices =
            ZL_Encoder_createTypedStream(eictx, 1, nbElts, idxWidth);
    ZL_ERR_IF_NULL(indices, allocation);

    // Allocate buffer for key manipulation
    VSFKey* const keysBuffer =
            ZL_Encoder_getScratchSpace(eictx, alphabetSize * sizeof(VSFKey));
    ZL_ERR_IF_NULL(keysBuffer, allocation);

    ZL_ERR_IF_ERR(ZS_tokenizeVSFEncode(
            ZL_Output_ptr(alphabet),
            alphabetFieldSizes,
            alphabetSize,
            ZL_Output_ptr(indices),
            keysBuffer,
            src,
            fieldSizes,
            nbElts,
            tokToIdx,
            idxWidth,
            sort));

    ZL_ERR_IF_ERR(ZL_Output_commit(alphabet, alphabetSize));
    ZL_ERR_IF_ERR(ZL_Output_commit(indices, nbElts));

    return ZL_returnSuccess();
}

ZL_Report EI_tokenizeVSF(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    MapVSF tokToIdx    = MapVSF_create((uint32_t)ZL_Input_numElts(in) + 1);
    ZL_Report report   = EI_tokenizeVSFImpl(
            eictx, &tokToIdx, in, EI_tokenizeShouldSort(eictx));
    MapVSF_destroy(&tokToIdx);
    return report;
}

ZL_GraphID ZL_Compressor_registerTokenizeGraph(
        ZL_Compressor* compressor,
        ZL_Type inputType,
        bool sort,
        ZL_GraphID alphabetGraph,
        ZL_GraphID indicesGraph)
{
    ZL_RESULT_OF(ZL_GraphID)
    graphResult = ZL_Compressor_buildTokenizeGraph(
            compressor, inputType, sort, alphabetGraph, indicesGraph);
    if (ZL_RES_isError(graphResult)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(graphResult);
    }
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_parameterizeTokenizeNode(
        ZL_Compressor* compressor,
        ZL_Type inputType,
        bool sort)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_NodeID, compressor);
    ZL_NodeID node;
    switch (inputType) {
        case ZL_Type_struct:
            node = ZL_NODE_TOKENIZE_STRUCT;
            ZL_ERR_IF(
                    sort,
                    graph_invalid,
                    "Tokenize: Struct does not support sorting");
            break;
        case ZL_Type_numeric:
            node = ZL_NODE_TOKENIZE_NUMERIC;
            break;
        case ZL_Type_string:
            node = ZL_NODE_TOKENIZE_STRING;
            break;
        case ZL_Type_serial:
        default:
            ZL_ERR(graph_invalid, "Tokenize: Invalid type %d", inputType);
    }
    if (sort) {
        ZL_NodeParameters params = {
            .name        = "tokenize_sorted",
            .localParams = &ZL_LP_1INTPARAM(ZL_TOKENIZE_SORT_PID, 1),
        };
        ZL_TRY_LET_CONST(
                ZL_NodeID,
                sortNode,
                ZL_Compressor_parameterizeNode(compressor, node, &params));
        node = sortNode;
    }
    return ZL_WRAP_VALUE(node);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildTokenizeGraph(
        ZL_Compressor* compressor,
        ZL_Type inputType,
        bool sort,
        ZL_GraphID alphabetGraph,
        ZL_GraphID indicesGraph)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);
    ZL_TRY_LET_CONST(
            ZL_NodeID,
            node,
            ZL_Compressor_parameterizeTokenizeNode(
                    compressor, inputType, sort));
    return ZL_Compressor_buildStaticGraph(
            compressor, node, ZL_GRAPHLIST(alphabetGraph, indicesGraph), NULL);
}

ZL_NodeID ZS2_createNode_customTokenize(
        ZL_Compressor* cgraph,
        ZL_Type streamType,
        ZL_CustomTokenizeFn customTokenizeFn,
        void const* opaque)
{
    if (streamType != ZL_Type_struct) {
        // TODO(terrelln): Log a warning here
        return ZL_NODE_ILLEGAL;
    }
    ZL_CustomTokenize_Param param = {
        .customTokenizeFn = customTokenizeFn,
        .opaque           = opaque,
    };
    ZL_LocalParams const lParams = ZL_LP_1COPYPARAM(
                    .paramId   = ZL_TOKENIZE_TOKENIZER_PID,
                    .paramPtr  = &param,
                    .paramSize = sizeof(param));
    return ZL_Compressor_registerParameterizedNode(
            cgraph,
            &(const ZL_ParameterizedNodeDesc){
                    .node        = ZL_NODE_TOKENIZE,
                    .localParams = &lParams,
            });
}

ZL_GraphID ZL_Compressor_registerCustomTokenizeGraph(
        ZL_Compressor* cgraph,
        ZL_Type streamType,
        ZL_CustomTokenizeFn customTokenizeFn,
        void const* opaque,
        ZL_GraphID alphabetGraph,
        ZL_GraphID indicesGraph)
{
    ZL_NodeID node = ZS2_createNode_customTokenize(
            cgraph, streamType, customTokenizeFn, opaque);
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, ZL_GRAPHLIST(alphabetGraph, indicesGraph));
}
