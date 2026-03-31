// Copyright (c) Meta Platforms, Inc. and affiliates.

// Suppress MSVC warning C4200 for flexible array members (C99 feature)
#ifdef _MSC_VER
#    pragma warning(disable : 4200)
#endif

#define FSE_STATIC_LINKING_ONLY

#include "openzl/codecs/entropy/deprecated/encode_fse_kernel.h"

#include <stddef.h> // size_t

#include "openzl/codecs/entropy/deprecated/common_fse_kernel.h"
#include "openzl/fse/bitstream.h"
#include "openzl/fse/fse.h"
#include "openzl/shared/utils.h"

typedef struct {
    uint32_t counts[256];
    uint32_t total;
    uint32_t maxCount;
    uint32_t maxSymbol;
} ZL_Histogram;

static ZL_Histogram* ZS_computeHistograms(
        uint8_t const* src,
        uint8_t const* ctx,
        size_t size,
        ZL_ContextClustering const* clustering)
{
    size_t const histsSize    = sizeof(ZL_Histogram) * clustering->numClusters;
    ZL_Histogram* const hists = malloc(histsSize);
    ZL_REQUIRE_NN(hists);
    memset(hists, 0, histsSize);
    for (size_t i = 0; i < size; ++i) {
        size_t const cluster     = clustering->contextToCluster[ctx[i]];
        ZL_Histogram* const hist = &hists[cluster];
        ++hist->counts[src[i]];
        ++hist->total;
        if (src[i] > hist->maxSymbol) {
            hist->maxSymbol = src[i];
        }
        if (hist->counts[src[i]] > hist->maxCount) {
            hist->maxCount = hist->counts[src[i]];
        }
    }

    return hists;
}

typedef struct {
    FSE_CState_t state;
    // TODO: Shrink this.
    FSE_CTable
            table[FSE_CTABLE_SIZE_U32(FSE_MAX_TABLELOG, FSE_MAX_SYMBOL_VALUE)];
} ZS_FseClusterCState;

typedef struct {
    FSE_CState_t* contextToState[256];
    ZS_FseClusterCState clusters[];
} ZS_FseClusterCStates;

ZL_Report ZS_fseContextEncode(
        ZL_WC* dst,
        ZL_RC* src,
        ZL_RC* ctx,
        ZL_ContextClustering const* clustering)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const numClusters = clustering->numClusters;
    ZL_ASSERT_LE(numClusters, 256);
    ZL_ASSERT_EQ(ZL_RC_avail(ctx), ZL_RC_avail(src));

    //> Write the number of symbols
    // TODO: Do we want to write the number of symbols here?
    // We need it if any states are Constant.
    {
        uint64_t const nbSymbols = ZL_RC_avail(src);
        ZL_WC_REQUIRE_HAS(dst, ZL_varintSize(nbSymbols));
        ZL_WC_pushVarint(dst, nbSymbols);
    }

    //> Handle empty input corner case
    if (ZL_RC_avail(src) == 0) {
        return ZL_returnSuccess();
    }

    //> Write the clustering
    ZL_ERR_IF_ERR(ZL_ContextClustering_encode(dst, clustering));

    //> Initialize the FSE infos & write the NCounts
    ZS_FseClusterCStates* states;
    {
        //> Compute the histograms
        ZL_Histogram* const hists = ZS_computeHistograms(
                ZL_RC_ptr(src), ZL_RC_ptr(ctx), ZL_RC_avail(src), clustering);

        //> Compute the table space
        size_t tableSpace = numClusters * sizeof(ZS_FseClusterCState);

        //> Build the CTables & write the NCounts & initialize the cstate
        states = malloc(sizeof(ZS_FseClusterCStates) + tableSpace);
        ZL_REQUIRE_NN(states);
        {
            ZS_FseClusterCState* state = states->clusters;
            for (size_t cluster = 0; cluster < numClusters;
                 ++cluster, ++state) {
                ZL_Histogram const* hist = &hists[cluster];

                //> Reserve space for the mode header.
                ZL_WC_REQUIRE_HAS(dst, 2);
                uint8_t* hdr = ZL_WC_ptr(dst);
                ZL_WC_advance(dst, 1);
                ZS_FseTransformPrefix_e mode = ZS_FseTransformPrefix_fse;

                //> Check for RAW and Constant
                ZL_ASSERT_GT(hist->total, 0);
                if (hist->maxCount == hist->total) {
                    mode = ZS_FseTransformPrefix_constant;
                } else if (
                        hist->maxCount == 1
                        || hist->maxCount < (hist->total >> 7)) {
                    mode = ZS_FseTransformPrefix_lit;
                }

                //> Build the NCount
                short nCount[256];
                size_t tableLog;
                if (mode == ZS_FseTransformPrefix_fse) {
                    tableLog = FSE_optimalTableLog(
                            FSE_MAX_TABLELOG, hist->total, hist->maxSymbol);
                    tableLog = FSE_normalizeCount(
                            nCount,
                            (unsigned)tableLog,
                            hist->counts,
                            hist->total,
                            hist->maxSymbol,
                            /* useLowProbSymbols */ 1);
                    ZL_REQUIRE(!FSE_isError(tableLog));

                    //> Write the NCount
                    {
                        size_t const nCountSize = FSE_writeNCount(
                                ZL_WC_ptr(dst),
                                ZL_WC_avail(dst),
                                nCount,
                                hist->maxSymbol,
                                (unsigned)tableLog);
                        ZL_REQUIRE(!FSE_isError(nCountSize));

                        //> Switch to lit mode if the header is too large.
                        // TODO: Improve this heuristic to account for encoded
                        // size
                        if (hist->total <= nCountSize) {
                            mode = ZS_FseTransformPrefix_lit;
                        } else {
                            ZL_WC_advance(dst, nCountSize);
                        }
                    }
                }

                //> Write the header
                *hdr = (uint8_t)mode;

                //> Build the CTable
                switch (mode) {
                    case ZS_FseTransformPrefix_fse:
                        ZL_REQUIRE(!FSE_isError(FSE_buildCTable(
                                state->table,
                                nCount,
                                hist->maxSymbol,
                                (unsigned)tableLog)));
                        break;
                    case ZS_FseTransformPrefix_lit: {
                        unsigned const nbBits =
                                (unsigned)ZL_highbit32(hist->maxSymbol + 1) + 1;
                        ZL_WC_push(dst, (uint8_t)nbBits);
                        ZL_REQUIRE(!FSE_isError(
                                FSE_buildCTable_raw(state->table, nbBits)));
                        break;
                    }
                    case ZS_FseTransformPrefix_constant:
                        ZL_WC_push(dst, (uint8_t)hist->maxSymbol);
                        ZL_REQUIRE(!FSE_isError(FSE_buildCTable_constant(
                                state->table, (uint8_t)hist->maxSymbol)));
                        break;
                }

                //> Initialize the CState
                FSE_initCState(&state->state, state->table);
            }
        }
        free(hists);
    }

    //> Fill the contextToInfo map
    for (size_t context = 0; context < 256; ++context) {
        size_t const cluster            = clustering->contextToCluster[context];
        states->contextToState[context] = &states->clusters[cluster].state;
    }

    //> Initialize the bitstream
    BIT_CStream_t bits;
    BIT_initCStream(&bits, ZL_WC_ptr(dst), ZL_WC_avail(dst));

    //> FSE compress symbols in reverse order
    // TODO: Make this faster to decompress by interleaving streams?
    for (size_t i = 0, n = ZL_RC_avail(src); i < n; ++i) {
        FSE_CState_t* const state = states->contextToState[ZL_RC_rPop(ctx)];
        FSE_encodeSymbol(&bits, state, ZL_RC_rPop(src));
        BIT_flushBits(&bits);
    }

    //> Flush the CStates to the bitstream in reverse cluster order
    for (size_t cluster = numClusters; cluster-- > 0;) {
        FSE_flushCState(&bits, &states->clusters[cluster].state);
    }

    //> Close the bitstream and validate
    {
        size_t const cSize = BIT_closeCStream(&bits);
        ZL_REQUIRE_NE(cSize, 0);
        ZL_WC_advance(dst, cSize);
    }

    free(states);
    return ZL_returnSuccess();
}

ZL_Report
ZS_fseO1Encode(ZL_WC* dst, ZL_RC* src, ZL_ContextClustering const* clustering)
{
    //> Handle empty edge case
    if (ZL_RC_avail(src) == 0) {
        return ZL_returnSuccess();
    }

    //> Encode the first byte directly
    // TODO: Think about improving this.
    ZL_RC context = *src;
    ZL_RC_subtract(&context, 1);
    ZL_WC_REQUIRE_HAS(dst, 1);
    ZL_WC_move(dst, src, 1);

    ZL_ASSERT_EQ(ZL_RC_avail(src), ZL_RC_avail(&context));
    return ZS_fseContextEncode(dst, src, &context, clustering);
}

ZL_Report ZS_fseContextO1Encode(
        ZL_WC* dst,
        ZL_RC* src,
        ZL_RC* ctx,
        uint8_t (*mix)(void* opaque, uint8_t ctx, uint8_t o1),
        void* opaque,
        ZL_ContextClustering const* clustering)
{
    //> Handle empty edge case
    if (ZL_RC_avail(src) == 0) {
        return ZL_returnSuccess();
    }

    //> Encode the first byte directly
    // TODO: Think about improving this.
    uint8_t const* const o1 = ZL_RC_ptr(src);
    ZL_WC_REQUIRE_HAS(dst, 1);
    ZL_WC_move(dst, src, 1);
    ZL_RC_advance(ctx, 1);

    size_t const size    = ZL_RC_avail(ctx);
    uint8_t* const mixed = malloc(size);
    ZL_REQUIRE_NN(mixed);

    //> Write the mixed context
    for (size_t i = 0; i < size; ++i) {
        size_t const m = mix(opaque, ZL_RC_pop(ctx), o1[i]);
        ZL_ASSERT_UINT_FITS(m, uint8_t);
        mixed[i] = (uint8_t)m;
    }
    ZL_ASSERT_EQ(ZL_RC_avail(ctx), 0);

    //> FSE context compress
    ZL_RC mixedRC       = ZL_RC_wrap(mixed, size);
    ZL_Report const ret = ZS_fseContextEncode(dst, src, &mixedRC, clustering);

    free(mixed);

    return ret;
}
