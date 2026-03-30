// Copyright (c) Meta Platforms, Inc. and affiliates.

#define FSE_STATIC_LINKING_ONLY

#include "openzl/codecs/entropy/deprecated/decode_fse_kernel.h"

#include <stddef.h> // size_t

#include "openzl/codecs/entropy/deprecated/common_fse_kernel.h"
#include "openzl/fse/fse.h"
#include "openzl/shared/clustering.h"

#ifdef _MSC_VER
#    pragma warning(disable : 4200) // nonstandard extension used: zero-sized
                                    // array in struct/union
#endif

typedef struct {
    FSE_DState_t state;
    FSE_DTable table[FSE_DTABLE_SIZE_U32(FSE_MAX_TABLELOG)];
} ZS_FseClusterDState;

typedef struct {
    FSE_DState_t* contextToState[256];
    ZS_FseClusterDState clusters[];
} ZS_FseClusterDStates;

/// Handles both explicit and implicit context controlled by the O1 template
/// argument. If mix is NULL then no explicit context is used.
ZL_FORCE_INLINE ZL_Report ZS_fseContextDecodeImpl(
        ZL_WC* dst,
        ZL_RC* src,
        ZL_RC* ctx,
        uint8_t contextO1,
        void* opaque,
        uint8_t (*mix)(void* opaque, uint8_t ctx, uint8_t o1) /* template */,
        bool const O1 /* template */)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    //> Read the number of symbols
    ZL_TRY_LET_CONST(uint64_t, nbSymbols, ZL_RC_popVarint(src));

    ZL_ASSERT(mix == NULL || (ctx != NULL && ZL_RC_avail(ctx) >= nbSymbols));
    ZL_ASSERT(mix == NULL || O1);

    //> Handle empty input corner case
    if (nbSymbols == 0) {
        return ZL_returnSuccess();
    }

    //> Read the clustering
    ZL_ContextClustering clustering;
    ZL_ERR_IF_ERR(ZL_ContextClustering_decode(&clustering, src));
    size_t const numClusters = clustering.numClusters;

    //> Read the headers and build the table for each cluster
    ZS_FseClusterDStates* states =
            malloc(sizeof(ZS_FseClusterDStates)
                   + numClusters * sizeof(ZS_FseClusterDState));
    ZL_REQUIRE_NN(states);
    for (size_t cluster = 0; cluster < numClusters; ++cluster) {
        FSE_DTable* const table = states->clusters[cluster].table;

        //> Read the mode
        ZL_RC_REQUIRE_HAS(src, 2);
        ZS_FseTransformPrefix_e const mode = ZL_RC_pop(src);

        //> Build the DTable
        switch (mode) {
            case ZS_FseTransformPrefix_fse: {
                //> Read the NCount
                short nCount[256];
                unsigned max            = FSE_MAX_SYMBOL_VALUE;
                unsigned tableLog       = FSE_MAX_TABLELOG;
                size_t const nCountSize = FSE_readNCount(
                        nCount,
                        &max,
                        &tableLog,
                        ZL_RC_ptr(src),
                        ZL_RC_avail(src));
                ZL_REQUIRE(!FSE_isError(nCountSize));
                ZL_RC_advance(src, nCountSize);

                //> Build the table
                ZL_REQUIRE(!FSE_isError(
                        FSE_buildDTable(table, nCount, max, tableLog)));
                break;
            }
            case ZS_FseTransformPrefix_lit: {
                //> Read #bits and build the table
                unsigned const nbBits = ZL_RC_pop(src);
                ZL_REQUIRE(!FSE_isError(FSE_buildDTable_raw(table, nbBits)));
                break;
            }
            case ZS_FseTransformPrefix_constant: {
                //> Read the symbol and build the table
                uint8_t const symbol = ZL_RC_pop(src);
                ZL_REQUIRE(
                        !FSE_isError(FSE_buildDTable_constant(table, symbol)));
                break;
            }
        }
    }

    //> Open the bitstream
    BIT_DStream_t bits;
    ZL_REQUIRE(!FSE_isError(
            BIT_initDStream(&bits, ZL_RC_ptr(src), ZL_RC_avail(src))));

    //> Initialize the DStates
    for (size_t cluster = 0; cluster < numClusters; ++cluster) {
        ZS_FseClusterDState* state = &states->clusters[cluster];
        FSE_initDState(&state->state, &bits, state->table);
    }

    //> Build the contextToState map
    for (size_t context = 0; context < 256; ++context) {
        size_t const cluster            = clustering.contextToCluster[context];
        states->contextToState[context] = &states->clusters[cluster].state;
    }

    //> Decompress the symbols
    // TODO: Optimize
    ZL_WC_REQUIRE_HAS(dst, nbSymbols);
    for (uint64_t i = 0; i < nbSymbols; ++i) {
        size_t context;
        if (O1) {
            if (mix) {
                context = mix(opaque, ZL_RC_pop(ctx), contextO1);
                ZL_ASSERT_UINT_FITS(context, uint8_t);
            } else {
                context = contextO1;
            }
        } else {
            context = ZL_RC_pop(ctx);
        }
        FSE_DState_t* const state = states->contextToState[context];
        uint8_t const symbol      = FSE_decodeSymbol(state, &bits);
        if (O1) {
            contextO1 = symbol;
        }
        ZL_WC_push(dst, symbol);
        BIT_reloadDStream(&bits);
    }

    //> Validate and close the bitstream
    ZL_REQUIRE_EQ(BIT_reloadDStream(&bits), BIT_DStream_completed);
    ZL_RC_advance(src, ZL_RC_avail(src));

    free(states);

    return ZL_returnSuccess();
}

ZL_Report ZS_fseContextDecode(ZL_WC* dst, ZL_RC* src, ZL_RC* ctx)
{
    ZL_ASSERT_NN(ctx);
    return ZS_fseContextDecodeImpl(
            dst, src, ctx, 0, NULL, NULL, false /* O1 */);
}

ZL_Report ZS_fseO1Decode(ZL_WC* dst, ZL_RC* src)
{
    //> Handle empty edge case
    if (ZL_RC_avail(src) == 0) {
        return ZL_returnSuccess();
    }
    //> Decode the first byte
    ZL_RC_REQUIRE_HAS(src, 1);
    ZL_WC_REQUIRE_HAS(dst, 1);
    uint8_t const first = ZL_RC_pop(src);
    ZL_WC_push(dst, first);

    return ZS_fseContextDecodeImpl(
            dst, src, NULL, first, NULL, NULL, true /* O1 */);
}

ZL_Report ZS_fseContextO1Decode(
        ZL_WC* dst,
        ZL_RC* src,
        ZL_RC* ctx,
        uint8_t (*mix)(void* opaque, uint8_t ctx, uint8_t o1),
        void* opaque)
{
    ZL_REQUIRE_NN(mix);

    //> Handle empty edge case
    if (ZL_RC_avail(src) == 0) {
        return ZL_returnSuccess();
    }
    //> Decode the first byte
    ZL_RC_REQUIRE_HAS(src, 1);
    ZL_WC_REQUIRE_HAS(dst, 1);
    uint8_t const first = ZL_RC_pop(src);
    ZL_WC_push(dst, first);
    ZL_RC_advance(ctx, 1);

    return ZS_fseContextDecodeImpl(
            dst, src, ctx, first, opaque, mix, true /* O1 */);
}
