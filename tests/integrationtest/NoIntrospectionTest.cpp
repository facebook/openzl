// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/errors_internal.h"
#include "openzl/common/introspection.h" // WAYPOINT, ZL_CompressIntrospectionHooks
#include "openzl/compress/enc_interface.h" // ZL_Encoder_s
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_config.h" // ZL_ALLOW_INTROSPECTION
#include "openzl/zl_opaque_types.h"

#include "tests/datagen/DataGen.h"

#if ZL_ALLOW_INTROSPECTION
#    error "Test must be compiled with introspection disabled"
#endif

using namespace ::testing;

namespace openzl::tests {

namespace {

static int ctr = 0;

class IncrementingHooks : public ::openzl::CompressIntrospectionHooks {
   public:
    void on_ZL_CCtx_compressMultiTypedRef_start(
            const ZL_CCtx* const,
            const void* const,
            const size_t,
            const ZL_TypedRef* const*,
            const size_t) override
    {
        ++ctr;
        std::cerr << "hello world" << std::endl;
    }
};

} // namespace

TEST(NoIntrospectionTest, IFintrospectionNotEnabledTHENhooksDoNothing)
{
    IncrementingHooks hooks;
    ctr                 = 0;
    ZL_CCtx* const cctx = ZL_CCtx_create();
    EXPECT_NE(nullptr, cctx);
    EXPECT_FALSE(ZL_isError(
            ZL_CCtx_attachIntrospectionHooks(cctx, hooks.getRawHooks())));
    EXPECT_EQ(0, ctr);
    EXPECT_TRUE(ZL_isError(
            ZL_CCtx_compressMultiTypedRef(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(0, ctr);
    ZL_CCtx_free(cctx);
}

TEST(NoIntrospectionTest, IFnoHooksTHENnoop)
{
    ctr                 = 0;
    ZL_CCtx* const cctx = ZL_CCtx_create();
    EXPECT_NE(nullptr, cctx);

    // No hooks object attached
    EXPECT_TRUE(ZL_isError(ZL_CCtx_compress(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(0, ctr);

    // Null hook attached
    ZL_CompressIntrospectionHooks hooks = {};
    EXPECT_FALSE(ZL_isError(ZL_CCtx_attachIntrospectionHooks(cctx, &hooks)));
    EXPECT_TRUE(ZL_isError(
            ZL_CCtx_compressMultiTypedRef(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(0, ctr);
    ZL_CCtx_free(cctx);
}

} // namespace openzl::tests
