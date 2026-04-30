// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/localparams.h"

#include <limits.h> // INT_MIN, INT_MAX

#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h" // ZL_memcpy

#define XXH_INLINE_ALL
#include "openzl/shared/xxhash.h"

static ZL_Report
LP_transferBuffer(Arena* alloc, void const** bufferPtr, size_t nbytes)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_NN(bufferPtr);
    if (nbytes == 0) {
        *bufferPtr = NULL;
    } else {
        ZL_ASSERT_NN(alloc);
        void* const dst = ALLOC_Arena_malloc(alloc, nbytes);
        ZL_ERR_IF_NULL(dst, allocation);
        ZL_ASSERT_NN(*bufferPtr);
        ZL_memcpy(dst, *bufferPtr, nbytes);
        *bufferPtr = dst;
    }
    return ZL_returnSuccess();
}

/* LP_transferLocalIntParams():
 * @return : success, or error
 * note: after IntParams have been transferred,
 *       @lip is modified so that @.intParams points to safe storage area.
 */
static ZL_Report LP_transferLocalIntParams(Arena* alloc, ZL_LocalIntParams* lip)
{
    ZL_ASSERT_NN(alloc);
    ZL_ASSERT_NN(lip);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const nbIntParams = lip->nbIntParams;
    ZL_DLOG(TRANSFORM, "LP_transferLocalIntParams: nb=%zu", nbIntParams);

    void const* buffer = lip->intParams;
    ZL_ERR_IF_ERR(LP_transferBuffer(
            alloc, &buffer, nbIntParams * sizeof(lip->intParams[0])));
    lip->intParams = buffer;
    return ZL_returnSuccess();
}

/* LP_transferLocalRefParams():
 * @return : success, or error
 * note: after refParams have been transferred,
 *       @lrp is modified so that @.refParams points to safe storage area.
 */
static ZL_Report LP_transferLocalRefParams(Arena* alloc, ZL_LocalRefParams* lrp)
{
    ZL_ASSERT_NN(alloc);
    ZL_ASSERT_NN(lrp);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const nbRefParams = lrp->nbRefParams;
    ZL_DLOG(TRANSFORM, "LP_transferLocalRefParams: nb=%zu", nbRefParams);

    void const* buffer = lrp->refParams;
    ZL_ERR_IF_ERR(LP_transferBuffer(
            alloc, &buffer, nbRefParams * sizeof(lrp->refParams[0])));
    lrp->refParams = buffer;
    return ZL_returnSuccess();
}

static ZL_Report LP_transferLocalFlatParams(
        Arena* alloc,
        ZL_LocalCopyParams* lgp)
{
    ZL_ASSERT_NN(alloc);
    ZL_ASSERT_NN(lgp);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const nbCopyParams = lgp->nbCopyParams;
    ZL_DLOG(TRANSFORM, "LP_transferLocalGenParams: nb=%zu", nbCopyParams);

    ZL_CopyParam* dstParams =
            ALLOC_Arena_malloc(alloc, nbCopyParams * sizeof(ZL_CopyParam));
    ZL_ERR_IF_NULL(dstParams, allocation);

    // transfer generic parameter's content into local storage
    // to not depend on origin's lifetime
    for (size_t n = 0; n < nbCopyParams; n++) {
        size_t const size   = lgp->genParams[n].paramSize;
        void const* content = lgp->genParams[n].paramPtr;
        ZL_ERR_IF_ERR(LP_transferBuffer(alloc, &content, size));
        ZL_CopyParam const gp = {
            .paramId   = lgp->genParams[n].paramId,
            .paramPtr  = content,
            .paramSize = size,
        };
        dstParams[n] = gp;
    }
    lgp->genParams = dstParams;
    return ZL_returnSuccess();
}

ZL_Report LP_transferLocalParams(Arena* alloc, ZL_LocalParams* lp)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_ERR(LP_transferLocalIntParams(alloc, &lp->intParams));
    ZL_ERR_IF_ERR(LP_transferLocalFlatParams(alloc, &lp->copyParams));
    ZL_ERR_IF_ERR(LP_transferLocalRefParams(alloc, &lp->refParams));
    return ZL_returnSuccess();
}

/* ====   Accessors   ==== */

ZL_LocalIntParams LP_getLocalIntParams(const ZL_LocalParams* lp)
{
    ZL_DLOG(SEQ, "LP_getLocalIntParams (LocalParam address: %p)", lp);
    if (lp == NULL)
        return (ZL_LocalIntParams){ NULL, 0 };
    return lp->intParams;
}

/* Note: this implementation presumes that @nbIntParams is rather small */
ZL_IntParam LP_getLocalIntParam(const ZL_LocalParams* lps, int intParamId)
{
    ZL_DLOG(SEQ, "LP_getLocalIntParam (id=%i)", intParamId);
    ZL_ASSERT_NN(lps);
    const ZL_LocalIntParams* const lip = &lps->intParams;
    ZL_ASSERT_NN(lip);
    ZL_DLOG(SEQ, "nbIntParams=%zu", lip->nbIntParams);
    for (size_t n = 0; n < lip->nbIntParams; n++) {
        if (lip->intParams[n].paramId == intParamId) {
            return lip->intParams[n];
        }
    }
    return (ZL_IntParam){ .paramId = ZL_LP_INVALID_PARAMID, .paramValue = 0 };
}

ZL_RefParam LP_getLocalRefParam(const ZL_LocalParams* lp, int refParamId)
{
    ZL_DLOG(TRANSFORM, "LP_getLocalRefParam (refParamId=%i)", refParamId);
    ZL_ASSERT_NN(lp);
    /* check the refParam storage */
    ZL_LocalRefParams const lrp = lp->refParams;
    for (size_t n = 0; n < lrp.nbRefParams; n++) {
        if (lrp.refParams[n].paramId == refParamId) {
            return lrp.refParams[n];
        }
    }
    /* check if present as a flat buffer param */
    ZL_LocalCopyParams const lgp = lp->copyParams;
    for (size_t n = 0; n < lgp.nbCopyParams; n++) {
        if (lgp.copyParams[n].paramId == refParamId) {
            return (ZL_RefParam){ .paramId   = refParamId,
                                  .paramRef  = lgp.copyParams[n].paramPtr,
                                  .paramSize = lgp.copyParams[n].paramSize };
        }
    }
    /* not found*/
    return (ZL_RefParam){ .paramId   = ZL_LP_INVALID_PARAMID,
                          .paramRef  = NULL,
                          .paramSize = 0 };
}

#define HASH_RESET(hs)                                 \
    do {                                               \
        const bool _xxh_error = XXH3_64bits_reset(hs); \
        ZL_ASSERT(!_xxh_error);                        \
    } while (0)

#define HASH_UPDATE(ptr, size)                                         \
    do {                                                               \
        const bool _xxh_error = XXH3_64bits_update(hs, (ptr), (size)); \
        ZL_ASSERT(!_xxh_error);                                        \
    } while (0)

ZL_INLINE void ZL_LocalIntParams_hash_inner(
        XXH3_state_t* hs,
        const ZL_LocalIntParams* const lip)
{
    size_t nbParamsHashed = 0;
    if (lip->nbIntParams == 0) {
        goto _finish;
    }
    ZL_ASSERT_NN(lip->intParams);
    if (lip->intParams == NULL) {
        goto _finish;
    }

    // Make sure we hash params in order, and only the first of each id...
    int prevParamIdPlusOne = INT_MIN;
    while (1) {
        const ZL_IntParam* curParam = NULL;
        int curParamId              = INT_MAX;
        for (size_t i = 0; i < lip->nbIntParams; i++) {
            const ZL_IntParam* const p = &lip->intParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (p->paramId < curParamId || curParam == NULL) {
                    curParam = p;
                }
                curParamId = p->paramId;
            }
        }

        if (curParam != NULL) {
            HASH_UPDATE(&curParam->paramId, sizeof(curParam->paramId));
            HASH_UPDATE(&curParam->paramValue, sizeof(curParam->paramValue));
            nbParamsHashed++;
        }

        if (curParamId == INT_MAX) {
            break;
        }
        prevParamIdPlusOne = curParamId + 1;
    }

_finish:
    HASH_UPDATE(&nbParamsHashed, sizeof(nbParamsHashed));
}

ZL_INLINE void ZL_LocalCopyParams_hash_inner(
        XXH3_state_t* const hs,
        const ZL_LocalCopyParams* const lcp)
{
    size_t nbParamsHashed = 0;
    if (lcp->nbCopyParams == 0) {
        goto _finish;
    }
    ZL_ASSERT_NN(lcp->copyParams);
    if (lcp->copyParams == NULL) {
        goto _finish;
    }

    // Make sure we hash params in order, and only the first of each id...
    int prevParamIdPlusOne = INT_MIN;
    while (1) {
        const ZL_CopyParam* curParam = NULL;
        int curParamId               = INT_MAX;
        for (size_t i = 0; i < lcp->nbCopyParams; i++) {
            const ZL_CopyParam* const p = &lcp->copyParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (p->paramId < curParamId || curParam == NULL) {
                    curParam = p;
                }
                curParamId = p->paramId;
            }
        }

        if (curParam != NULL) {
            HASH_UPDATE(&curParam->paramId, sizeof(curParam->paramId));
            HASH_UPDATE(&curParam->paramSize, sizeof(curParam->paramSize));
            HASH_UPDATE(curParam->paramPtr, curParam->paramSize);
            nbParamsHashed++;
        }

        if (curParamId == INT_MAX) {
            break;
        }
        prevParamIdPlusOne = curParamId + 1;
    }

_finish:
    HASH_UPDATE(&nbParamsHashed, sizeof(nbParamsHashed));
}

ZL_INLINE void ZL_LocalRefParams_hash_inner(
        XXH3_state_t* const hs,
        const ZL_LocalRefParams* const lrp)
{
    size_t nbParamsHashed = 0;
    if (lrp->nbRefParams == 0) {
        goto _finish;
    }
    ZL_ASSERT_NN(lrp->refParams);
    if (lrp->refParams == NULL) {
        goto _finish;
    }

    // Make sure we hash params in order, and only the first of each id...
    int prevParamIdPlusOne = INT_MIN;
    while (1) {
        const ZL_RefParam* curParam = NULL;
        int curParamId              = INT_MAX;
        for (size_t i = 0; i < lrp->nbRefParams; i++) {
            const ZL_RefParam* const p = &lrp->refParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (p->paramId < curParamId || curParam == NULL) {
                    curParam = p;
                }
                curParamId = p->paramId;
            }
        }

        if (curParam != NULL) {
            HASH_UPDATE(&curParam->paramId, sizeof(curParam->paramId));
            HASH_UPDATE(&curParam->paramRef, sizeof(curParam->paramRef));
            nbParamsHashed++;
        }

        if (curParamId == INT_MAX) {
            break;
        }
        prevParamIdPlusOne = curParamId + 1;
    }

_finish:
    HASH_UPDATE(&nbParamsHashed, sizeof(nbParamsHashed));
}

size_t ZL_LocalIntParams_hash(const ZL_LocalIntParams* const lip)
{
    if (lip == NULL) {
        return 0;
    }
    XXH3_state_t hs;
    XXH3_INITSTATE(&hs);
    HASH_RESET(&hs);
    ZL_LocalIntParams_hash_inner(&hs, lip);
    return XXH3_64bits_digest(&hs);
}

size_t ZL_LocalCopyParams_hash(const ZL_LocalCopyParams* const lcp)
{
    if (lcp == NULL) {
        return 0;
    }
    XXH3_state_t hs;
    XXH3_INITSTATE(&hs);
    HASH_RESET(&hs);
    ZL_LocalCopyParams_hash_inner(&hs, lcp);
    return XXH3_64bits_digest(&hs);
}

size_t ZL_LocalRefParams_hash(const ZL_LocalRefParams* const lrp)
{
    if (lrp == NULL) {
        return 0;
    }
    XXH3_state_t hs;
    XXH3_INITSTATE(&hs);
    HASH_RESET(&hs);
    ZL_LocalRefParams_hash_inner(&hs, lrp);
    return XXH3_64bits_digest(&hs);
}

size_t ZL_LocalParams_hash(const ZL_LocalParams* const lp)
{
    if (lp == NULL) {
        return 0;
    }
    XXH3_state_t hs;
    XXH3_INITSTATE(&hs);
    HASH_RESET(&hs);

    ZL_LocalIntParams_hash_inner(&hs, &lp->intParams);
    ZL_LocalCopyParams_hash_inner(&hs, &lp->copyParams);
    ZL_LocalRefParams_hash_inner(&hs, &lp->refParams);

    return XXH3_64bits_digest(&hs);
}

bool ZL_LocalIntParams_eq(
        const ZL_LocalIntParams* const lhs,
        const ZL_LocalIntParams* const rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return lhs == rhs;
    }

    int prevParamIdPlusOne = INT_MIN;
    while (1) {
        const ZL_IntParam* l = NULL;
        const ZL_IntParam* r = NULL;
        int curParamId       = INT_MAX;
        for (size_t i = 0; i < lhs->nbIntParams; i++) {
            const ZL_IntParam* const p = &lhs->intParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (curParamId != p->paramId || l == NULL) {
                    l = p;
                }
                curParamId = p->paramId;
            }
        }
        for (size_t i = 0; i < rhs->nbIntParams; i++) {
            const ZL_IntParam* const p = &rhs->intParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (curParamId != p->paramId) {
                    l = NULL;
                }
                if (curParamId != p->paramId || r == NULL) {
                    r = p;
                }
                curParamId = p->paramId;
            }
        }

        if (l == NULL || r == NULL) {
            return l == r;
        }

        ZL_ASSERT_EQ(l->paramId, r->paramId);
        if (l->paramValue != r->paramValue) {
            return false;
        }

        if (curParamId == INT_MAX) {
            return true;
        }
        prevParamIdPlusOne = curParamId + 1;
    }
}

bool ZL_LocalCopyParams_eq(
        const ZL_LocalCopyParams* const lhs,
        const ZL_LocalCopyParams* const rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return lhs == rhs;
    }

    int prevParamIdPlusOne = INT_MIN;
    while (1) {
        const ZL_CopyParam* l = NULL;
        const ZL_CopyParam* r = NULL;
        int curParamId        = INT_MAX;
        for (size_t i = 0; i < lhs->nbCopyParams; i++) {
            const ZL_CopyParam* const p = &lhs->copyParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (curParamId != p->paramId || l == NULL) {
                    l = p;
                }
                curParamId = p->paramId;
            }
        }
        for (size_t i = 0; i < rhs->nbCopyParams; i++) {
            const ZL_CopyParam* const p = &rhs->copyParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (curParamId != p->paramId) {
                    l = NULL;
                }
                if (curParamId != p->paramId || r == NULL) {
                    r = p;
                }
                curParamId = p->paramId;
            }
        }

        if (l == NULL || r == NULL) {
            return l == r;
        }

        ZL_ASSERT_EQ(l->paramId, r->paramId);
        if (l->paramSize != r->paramSize) {
            return false;
        }
        if (l->paramPtr == NULL || r->paramPtr == NULL) {
            if (l->paramPtr != r->paramPtr) {
                return false;
            }
            if (l->paramSize != 0 || r->paramSize != 0) {
                return false;
            }
        } else if (memcmp(l->paramPtr, r->paramPtr, l->paramSize)) {
            return false;
        }

        if (curParamId == INT_MAX) {
            return true;
        }
        prevParamIdPlusOne = curParamId + 1;
    }
}

bool ZL_LocalRefParams_eq(
        const ZL_LocalRefParams* const lhs,
        const ZL_LocalRefParams* const rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return lhs == rhs;
    }

    int prevParamIdPlusOne = INT_MIN;
    while (1) {
        const ZL_RefParam* l = NULL;
        const ZL_RefParam* r = NULL;
        int curParamId       = INT_MAX;
        for (size_t i = 0; i < lhs->nbRefParams; i++) {
            const ZL_RefParam* const p = &lhs->refParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (curParamId != p->paramId || l == NULL) {
                    l = p;
                }
                curParamId = p->paramId;
            }
        }
        for (size_t i = 0; i < rhs->nbRefParams; i++) {
            const ZL_RefParam* const p = &rhs->refParams[i];
            if (p->paramId >= prevParamIdPlusOne && p->paramId <= curParamId) {
                if (curParamId != p->paramId) {
                    l = NULL;
                }
                if (curParamId != p->paramId || r == NULL) {
                    r = p;
                }
                curParamId = p->paramId;
            }
        }

        if (l == NULL || r == NULL) {
            return l == r;
        }

        ZL_ASSERT_EQ(l->paramId, r->paramId);
        if (l->paramRef != r->paramRef) {
            return false;
        }

        if (curParamId == INT_MAX) {
            return true;
        }
        prevParamIdPlusOne = curParamId + 1;
    }
}

bool ZL_LocalParams_eq(
        const ZL_LocalParams* const lhs,
        const ZL_LocalParams* const rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return lhs == rhs;
    }
    return ZL_LocalIntParams_eq(&lhs->intParams, &rhs->intParams)
            && ZL_LocalCopyParams_eq(&lhs->copyParams, &rhs->copyParams)
            && ZL_LocalRefParams_eq(&lhs->refParams, &rhs->refParams);
}
