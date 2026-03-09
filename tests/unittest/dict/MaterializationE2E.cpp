// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include "zl_materializerregistry.h"

#include "openzl/zl_ctransform.h"
#include "openzl/zl_dtransform.h"

using namespace ::testing;

TEST(MaterializationE2E, HowToRegisterANode)
{
    ZL_IDType trid = ZL_IDType{ 67 };
    ZL_MIGraphDesc graphDesc{
        .CTid       = trid,
        .inputTypes = nullptr,
        // other stuff here
    };
    ZL_MIEncoderDesc encoderDesc{
        .gd          = graphDesc,
        .transform_f = nullptr,
        .name        = "enc",
    };
    ZL_MIDecoderDesc decoderDesc{
        .gd          = graphDesc,
        .transform_f = nullptr,
        .name        = "dec",
    };
    ZL_MaterializerDesc matDesc = {
        .codecID         = trid, // new field
        .materializeFn   = nullptr,
        .dematerializeFn = nullptr,
    };

    ZL_Compressor* compressor;
    ZL_Compressor_registerMIEncoder(compressor, &encoderDesc);

    ZL_DCtx* dctx;
    ZL_DCtx_registerMIDecoder(dctx, &decoderDesc);

    ZL_MaterializerRegistry* registry;
    ZL_MaterializerRegistry_registerNode(registry, &matDesc);
}

TEST(MaterializationE2E, HowToUse)
{
    ZL_MaterializerRegistry* registry; // pre-existing

    ZL_DictLoader loader(registry);
    ZL_DictID id;
    loader->fetchDict(id); // no need to pass a compressor or dctx--the registry
                           // has all the info to materialize and dematerialize
}
