// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdio.h>
#include <stdlib.h>
#include "openzl/shared/clustering.h"
#include "openzl/shared/portability.h" // ZL_UNUSED

#define kDumpGraph 1
#define kGraphFile "clustering.dot"

ZL_Report ZL_ContextClustering_encode(
        ZL_WC* dst,
        ZL_ContextClustering const* clustering)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const size = 1 + clustering->maxSymbol + 1;
    if (ZL_WC_avail(dst) < size) {
        ZL_ERR(GENERIC);
    }

    //> Write max symbol value
    ZL_WC_push(dst, (uint8_t)clustering->maxSymbol);

    //> Write the contextToCluster map
    memcpy(ZL_WC_ptr(dst),
           clustering->contextToCluster,
           clustering->maxSymbol + 1);
    ZL_WC_advance(dst, clustering->maxSymbol + 1);

    return ZL_returnSuccess();
}

ZL_Report ZL_cluster(
        ZL_ContextClustering* clustering,
        ZL_RC src,
        ZL_RC context,
        uint32_t maxContext,
        size_t maxClusters,
        ZL_ClusteringMode mode)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    switch (mode) {
        case ZL_ClusteringMode_identity:
            ZL_ContextClustering_identity(clustering, context);
            break;
        case ZL_ClusteringMode_greedy:
            ZL_ContextClustering_greedy(
                    clustering, src, context, maxContext, maxClusters);
            break;
        case ZL_ClusteringMode_prune:
            ZL_ContextClustering_prune(
                    clustering, context, maxContext, maxClusters);
            break;
        default:
            ZL_ERR(GENERIC);
    }
    if (clustering->numClusters > maxClusters)
        ZL_ERR(GENERIC);
    return ZL_returnSuccess();
}

void ZL_ContextClustering_identity(ZL_ContextClustering* clustering, ZL_RC ctx)
{
    uint8_t present[256];
    memset(present, 0, sizeof(present));
    uint8_t const* ptr = ZL_RC_ptr(&ctx);
    if (ptr) {
        uint8_t const* const end = ptr + ZL_RC_avail(&ctx);
        for (; ptr < end; ++ptr) {
            present[*ptr] = 1;
        }
    }

    size_t cluster = 0;
    for (size_t context = 0; context < 256; ++context) {
        clustering->contextToCluster[context] = (uint8_t)cluster;
        cluster += present[context];
    }
    size_t maxSymbol = 255;
    while (!present[maxSymbol] && maxSymbol > 0) {
        --maxSymbol;
    }

    clustering->numClusters = cluster;
    clustering->maxSymbol   = maxSymbol;
}

typedef struct {
    uint32_t count[256];
    uint32_t total;
    uint32_t max;
    uint64_t entropyCost;
} ZL_Histogram;

/**
 * -log2(x / 256) lookup table for x in [0, 256).
 * If x == 0: Return 0
 * Else: Return floor(-log2(x / 256) * 256)
 */
static unsigned const kInverseProbabilityLog256[256] = {
    0,    2048, 1792, 1642, 1536, 1453, 1386, 1329, 1280, 1236, 1197, 1162,
    1130, 1100, 1073, 1047, 1024, 1001, 980,  960,  941,  923,  906,  889,
    874,  859,  844,  830,  817,  804,  791,  779,  768,  756,  745,  734,
    724,  714,  704,  694,  685,  676,  667,  658,  650,  642,  633,  626,
    618,  610,  603,  595,  588,  581,  574,  567,  561,  554,  548,  542,
    535,  529,  523,  517,  512,  506,  500,  495,  489,  484,  478,  473,
    468,  463,  458,  453,  448,  443,  438,  434,  429,  424,  420,  415,
    411,  407,  402,  398,  394,  390,  386,  382,  377,  373,  370,  366,
    362,  358,  354,  350,  347,  343,  339,  336,  332,  329,  325,  322,
    318,  315,  311,  308,  305,  302,  298,  295,  292,  289,  286,  282,
    279,  276,  273,  270,  267,  264,  261,  258,  256,  253,  250,  247,
    244,  241,  239,  236,  233,  230,  228,  225,  222,  220,  217,  215,
    212,  209,  207,  204,  202,  199,  197,  194,  192,  190,  187,  185,
    182,  180,  178,  175,  173,  171,  168,  166,  164,  162,  159,  157,
    155,  153,  151,  149,  146,  144,  142,  140,  138,  136,  134,  132,
    130,  128,  126,  123,  121,  119,  117,  115,  114,  112,  110,  108,
    106,  104,  102,  100,  98,   96,   94,   93,   91,   89,   87,   85,
    83,   82,   80,   78,   76,   74,   73,   71,   69,   67,   66,   64,
    62,   61,   59,   57,   55,   54,   52,   50,   49,   47,   46,   44,
    42,   41,   39,   37,   36,   34,   33,   31,   30,   28,   26,   25,
    23,   22,   20,   19,   17,   16,   14,   13,   11,   10,   8,    7,
    5,    4,    2,    1,
};

/**
 * Returns the cost in bits of encoding the distribution described by count
 * using the entropy bound.
 */
static uint64_t
ZSTD_entropyCost(unsigned const* count, unsigned const max, size_t const total)
{
    uint64_t cost = 0;
    unsigned s;
    if (count[max] == total)
        return 0;
    for (s = 0; s <= max; ++s) {
        uint64_t norm = ((256 * (uint64_t)count[s]) / (uint64_t)total);
        if (count[s] != 0 && norm == 0)
            norm = 1;
        ZL_ASSERT_LT(count[s], total);
        cost += (uint64_t)count[s] * (uint64_t)kInverseProbabilityLog256[norm];
    }
    return cost >> 8;
}

static void ZS_fillEntropyCost(ZL_Histogram* hist)
{
    hist->entropyCost = ZSTD_entropyCost(hist->count, hist->max, hist->total);
}

static uint64_t ZS_combinedEntropyCost(
        ZL_Histogram const* histA,
        ZL_Histogram const* histB)
{
    uint32_t const max   = ZL_MAX(histA->max, histB->max);
    uint64_t const total = histA->total + histB->total;

    if ((histA->max == histB->max || histA->total == 0 || histB->total == 0)
        && histA->count[max] + histB->count[max] == total)
        return 0;

    uint64_t cost = 0;
    for (size_t s = 0; s <= max; ++s) {
        uint64_t const count = histA->count[s] + histB->count[s];
        uint64_t const norm  = ZL_MAX((256 * count) / total, count > 0);
        ZL_ASSERT(count == 0 || norm > 0);
        ZL_ASSERT_LT(count, total);
        ZL_ASSERT_LT(norm, 256);
        cost += count * kInverseProbabilityLog256[norm];
    }
    return cost >> 8;
}

static int64_t ZS_combineLoss(
        ZL_Histogram const* histA,
        ZL_Histogram const* histB)
{
    int64_t const separateCost =
            (int64_t)(histA->entropyCost + histB->entropyCost);
    int64_t const combinedCost = (int64_t)ZS_combinedEntropyCost(histA, histB);
    // ZL_ASSERT_GE(combinedCost, separateCost);
    return combinedCost - separateCost;
}

static ZL_UNUSED void
ZS_Histogram_compute(ZL_Histogram* hist, uint32_t maxSymbol, ZL_RC src)
{
    uint8_t const* const ip = ZL_RC_ptr(&src);
    size_t const srcSize    = ZL_RC_avail(&src);
    memset(hist->count, 0, sizeof(hist->count));
    ZL_REQUIRE_UINT_FITS(srcSize, uint32_t);
    hist->total = (uint32_t)srcSize;
    for (size_t i = 0; i < srcSize; ++i) {
        ++hist->count[ip[i]];
    }
    while (maxSymbol > 0 && hist->count[maxSymbol] == 0) {
        --maxSymbol;
    }
    hist->max = (uint32_t)maxSymbol;
    ZS_fillEntropyCost(hist);
}

static void ZS_Histogram_combine(ZL_Histogram* dst, ZL_Histogram const* src)
{
    dst->total += src->total;
    dst->max = ZL_MAX(dst->max, src->max);
    for (size_t s = 0; s <= dst->max; ++s) {
        dst->count[s] += src->count[s];
    }
    ZS_fillEntropyCost(dst);
}

typedef struct {
    uint32_t nbContexts;
    uint32_t maxContext;
    ZL_Histogram hists[];
} ZS_Histograms;

static void ZS_Histograms_computeO1(
        ZS_Histograms* histograms,
        uint32_t maxContext,
        uint32_t maxSymbol,
        ZL_RC context,
        ZL_RC src)
{
    ZL_Histogram* const hists       = histograms->hists;
    uint8_t const* const contextPtr = ZL_RC_ptr(&context);
    uint8_t const* const srcPtr     = ZL_RC_ptr(&src);
    size_t const size               = ZL_RC_avail(&src);
    ZL_REQUIRE_EQ(ZL_RC_avail(&context), ZL_RC_avail(&src));
    for (size_t c = 0; c <= maxContext; ++c) {
        memset(hists[c].count, 0, sizeof(hists[c].count));
    }
    ZL_REQUIRE_UINT_FITS(size, uint32_t);
    for (size_t i = 0; i < size; ++i) {
        size_t const ctx = contextPtr[i];
        ZL_ASSERT_LE(ctx, maxContext);
        ++hists[ctx].count[srcPtr[i]];
    }
    for (size_t c = 0; c <= maxContext; ++c) {
        uint32_t total = 0;
        uint32_t max   = (uint32_t)maxSymbol;
        while (max > 0 && hists[c].count[max] == 0) {
            --max;
        }
        hists[c].max = max;
        for (uint32_t i = 0; i <= max; ++i) {
            total += hists[c].count[i];
        }
        hists[c].total = total;
        ZS_fillEntropyCost(hists + c);
    }
    while (maxContext > 0 && hists[maxContext].total == 0) {
        --maxContext;
    }
    uint32_t nbContexts = 0;
    for (size_t c = 0; c <= maxContext; ++c) {
        nbContexts += (hists[c].total > 0);
    }
    histograms->maxContext = maxContext;
    histograms->nbContexts = nbContexts;
}

typedef struct {
    uint32_t size;
    uint32_t context;
} ContextSize;

static int ContextSize_cmp(void const* lv, void const* rv)
{
    ContextSize const* const lhs = (ContextSize const* const)lv;
    ContextSize const* const rhs = (ContextSize const* const)rv;
    return (int)rhs->size - (int)lhs->size;
}

void ZL_ContextClustering_prune(
        ZL_ContextClustering* clustering,
        ZL_RC context,
        uint32_t maxContext,
        size_t maxClusters)
{
    ContextSize sizes[256];
    for (uint32_t c = 0; c <= maxContext; ++c) {
        sizes[c].size    = 0;
        sizes[c].context = c;
    }
    {
        uint8_t const* c = ZL_RC_ptr(&context);
        size_t const s   = ZL_RC_avail(&context);
        for (size_t i = 0; i < s; ++i) {
            sizes[c[i]].size++;
        }
    }

    while (maxContext > 0 && sizes[maxContext].size == 0)
        --maxContext;
    clustering->maxSymbol = maxContext;

    uint32_t const smallClusterSize = 300;
    if (maxClusters >= maxContext) {
        // Just prune
        uint32_t nextCluster  = 0;
        uint32_t smallCluster = 256;
        for (uint32_t c = 0; c <= maxContext; ++c) {
            ZL_ASSERT_EQ(sizes[c].context, c);
            if (sizes[c].size < smallClusterSize) {
                if (smallCluster == 256) {
                    smallCluster = nextCluster++;
                }
                clustering->contextToCluster[c] = (uint8_t)smallCluster;
            } else {
                clustering->contextToCluster[c] = (uint8_t)nextCluster++;
            }
        }
        clustering->numClusters = nextCluster;
    } else {
        qsort(sizes, maxContext + 1, sizeof(sizes[0]), &ContextSize_cmp);
        memset(clustering->contextToCluster,
               0,
               sizeof(clustering->contextToCluster));
        uint32_t cluster;
        for (cluster = 0; cluster < maxClusters - 1; ++cluster) {
            uint32_t const ctx = sizes[cluster].context;
            if (sizes[cluster].size < smallClusterSize)
                break;
            clustering->contextToCluster[ctx] = (uint8_t)(cluster + 1);
        }
        clustering->numClusters = cluster + 1;
    }
}

void ZL_ContextClustering_greedy(
        ZL_ContextClustering* clustering,
        ZL_RC src,
        ZL_RC context,
        uint32_t maxContext,
        size_t maxClusters)
{
    ZL_REQUIRE_LE(maxClusters, 256);
    ZS_Histograms* const hists = (ZS_Histograms*)malloc(
            sizeof(ZS_Histograms) + (1 + maxContext) * sizeof(ZL_Histogram));
    ZL_REQUIRE_NN(hists);

    //> Compute the histograms
    ZS_Histograms_computeO1(hists, maxContext, 255, context, src);

    //> Set up the identity clustering
    size_t nbClusters = hists->maxContext + 1;
    int c2c[256];
    for (size_t c = 0; c <= hists->maxContext; ++c) {
        c2c[c] = -1;
    }

    uint64_t totalCost = 0;
    for (uint32_t c = 0; c <= hists->maxContext; ++c) {
        totalCost += hists->hists[c].entropyCost;
    }

    //> Prune small contexts
    if (1) {
        uint32_t smallCluster     = 256;
        uint32_t const smallCount = 300;
        for (uint32_t c = 0; c <= hists->maxContext; ++c) {
            if (hists->hists[c].total < smallCount) {
                if (smallCluster == 256) {
                    smallCluster = c;
                } else {
                    --nbClusters;
                    totalCost -= hists->hists[smallCluster].entropyCost;
                    totalCost -= hists->hists[c].entropyCost;
                    ZS_Histogram_combine(
                            &hists->hists[smallCluster], &hists->hists[c]);
                    totalCost += hists->hists[smallCluster].entropyCost;
                    c2c[c] = (uint8_t)smallCluster;
                }
                // ZL_LOG(
                //     V9,
                //     "Prune %u -> %u (size = %u bytes | total = %u bytes)",
                //     c,
                //     smallCluster,
                //     (uint32_t)hists->hists[c].total,
                //     (uint32_t)(totalCost >> 3));
            }
        }
    }

    if (nbClusters > maxClusters) {
        //> Compute context losses
        size_t const nbContexts = hists->maxContext + 1;
        int64_t* losses = malloc(sizeof(int64_t) * nbContexts * nbContexts);
        ZL_REQUIRE_NN(losses);
        for (size_t c0 = 0; c0 <= hists->maxContext; ++c0) {
            if (c2c[c0] != -1)
                continue;
            for (size_t c1 = c0 + 1; c1 <= hists->maxContext; ++c1) {
                if (c2c[c1] != -1)
                    continue;
                losses[c0 * nbContexts + c1] =
                        ZS_combineLoss(&hists->hists[c0], &hists->hists[c1]);
            }
        }

        //> Merge the closest two closest contexts iteratively & recompute
        // context
        // distances
        while (nbClusters > maxClusters) {
            int64_t minLoss = 1ll << 62;
            uint32_t m0     = (uint32_t)-1;
            uint32_t m1     = (uint32_t)-1;
            for (uint32_t c0 = 0; c0 <= hists->maxContext; ++c0) {
                if (c2c[c0] != -1)
                    continue;
                for (uint32_t c1 = c0 + 1; c1 <= hists->maxContext; ++c1) {
                    if (c2c[c1] != -1)
                        continue;
                    int64_t const loss = losses[c0 * nbContexts + c1];
                    if (loss < minLoss) {
                        minLoss = loss;
                        m0      = c0;
                        m1      = c1;
                    }
                }
            }
            ZL_ASSERT_NE(minLoss, 1ll << 62);
            c2c[m1] = (uint8_t)m0;
            ZL_ASSERT_EQ(c2c[m0], -1);
            --nbClusters;
            totalCost -= hists->hists[m0].entropyCost;
            totalCost -= hists->hists[m1].entropyCost;
            ZS_Histogram_combine(&hists->hists[m0], &hists->hists[m1]);
            totalCost += hists->hists[m0].entropyCost;
            // ZL_LOG(
            //     V9,
            //     "Merge %u -> %u (loss = %u bytes | total = %u bytes)",
            //     m1,
            //     m0,
            //     (uint32_t)(minLoss >> 3),
            //     (uint32_t)(totalCost >> 3));
            for (uint32_t c = 0; c <= hists->maxContext; ++c) {
                if (c2c[c] != -1 || c == m0)
                    continue;
                uint32_t const c0 = (c < m0) ? c : m0;
                uint32_t const c1 = (c < m0) ? m0 : c;
                losses[c0 * nbContexts + c1] =
                        ZS_combineLoss(&hists->hists[c0], &hists->hists[c1]);
            }
        }
        free(losses);
    }

    uint32_t nextCluster = 0;
    ZL_ASSERT_EQ(c2c[0], -1);
    for (size_t c = 0; c <= hists->maxContext; ++c) {
        if (c2c[c] == -1) {
            clustering->contextToCluster[c] = (uint8_t)nextCluster++;
        } else {
            ZL_ASSERT_LT(c2c[c], (int)c);
            clustering->contextToCluster[c] =
                    clustering->contextToCluster[c2c[c]];
        }
    }
    ZL_ASSERT_GT(nextCluster, 0);
    ZL_ASSERT_LE(nextCluster, maxClusters);
    clustering->numClusters = nextCluster;
    clustering->maxSymbol   = hists->maxContext;

    if (kDumpGraph) {
        FILE* graph = fopen(kGraphFile, "w");
        fprintf(graph, "digraph clusterint {\n");
        fprintf(graph, "\tnode [fontname=\"Arial\"];\n");
        for (unsigned c = 0; c <= hists->maxContext; ++c) {
            if (c2c[c] == -1) {
                fprintf(graph,
                        "\t%u -> Cluster_%u;\n",
                        (unsigned)c,
                        (unsigned)clustering->contextToCluster[c]);
            } else {
                fprintf(graph, "\t%u -> %u;\n", (unsigned)c, (unsigned)c2c[c]);
            }
        }
        fprintf(graph, "}\n");
        fclose(graph);
    }

    free(hists);

    ZL_LOG(TRANSFORM, "Final cost = %u bytes", (uint32_t)(totalCost >> 3));
}
