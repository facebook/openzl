// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/trStates.h"
#include "openzl/common/limits.h"
#include "openzl/compress/cnode.h"          // CNode
#include "openzl/compress/compress_types.h" // node_internalTransform

void TRS_init(CachedStates* trs)
{
    trs->states = CachedStatesMap_create(ZL_ENCODER_CUSTOM_NODE_LIMIT);
}

void TRS_destroy(CachedStates* trs)
{
    CachedStatesMap_Iter iter = CachedStatesMap_iter(&trs->states);
    for (CachedStatesMap_Entry const* entry;
         (entry = CachedStatesMap_Iter_next(&iter));) {
        ZL_ASSERT_NN(entry->key.stateFree);
        entry->key.stateFree(entry->val);
    }
    CachedStatesMap_destroy(&trs->states);
}

size_t TRS_sizeof(const CachedStates* trs)
{
    if (trs == NULL) {
        return 0;
    }
    size_t size               = CachedStatesMap_sizeof(&trs->states);
    CachedStatesMap_Iter iter = CachedStatesMap_iter(&trs->states);
    for (CachedStatesMap_Entry const* entry;
         (entry = CachedStatesMap_Iter_next(&iter));) {
        size += ZL_CodecStateManager_sizeof(&entry->key, entry->val);
    }
    return size;
}

void* TRS_getCodecState(CachedStates* trs, const CNode* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    ZL_CodecStateManager const trsm =
            cnode->transformDesc.publicDesc.trStateMgr;
    if (trsm.stateAlloc == NULL) {
        /* no allocation function (should not happen) */
        return NULL;
    }
    ZL_ASSERT_NN(trsm.stateFree);

    // look for cached state
    {
        ZL_ASSERT_NN(trs);
        const CachedStatesMap_Entry* const csme =
                CachedStatesMap_find(&trs->states, &trsm);
        if (csme != NULL) {
            // Previously cached state found
            return csme->val;
        }
    }
    // State not found -> create one
    void* const trState = trsm.stateAlloc();
    if (trState == NULL) {
        // State creation failed
        return NULL;
    }
    const CachedStatesMap_Entry newStateE = { trsm, trState };
    CachedStatesMap_Insert insertResult =
            CachedStatesMap_insert(&trs->states, &newStateE);
    if (insertResult.ptr) {
        return trState;
    }
    // Inserting the state failed
    trsm.stateFree(trState);
    return NULL;
}
