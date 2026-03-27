// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/zl_config.h"

#if ZL_ALLOW_INTROSPECTION

#    include <gtest/gtest.h>

#    include "cpp/tests/experimental/trace/TraceTestHelpers.hpp"
#    include "openzl/cpp/CCtx.hpp"
#    include "openzl/cpp/Compressor.hpp"

using namespace ::testing;

namespace openzl {

// Smoke test: verify TraceTestHelpers can parse a successful compression trace.
TEST(CompressTraceErrorTest, ParseHelperSmokeTest)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);

    std::vector<int64_t> data(1000, 42);
    auto input = Input::refNumeric(data.data(), sizeof(int64_t), data.size());

    CCtx cctx;
    cctx.refCompressor(compressor);
    cctx.writeTraces(true);

    auto compressed  = cctx.compressOne(input);
    auto traceResult = cctx.getLatestTrace();

    ASSERT_FALSE(traceResult.first.empty());

    auto parsed = test::parseTrace(traceResult.first);
    ASSERT_TRUE(parsed.has_value()) << "Failed to parse trace CBOR";

    EXPECT_EQ(parsed->operationType, 0) << "Should be compression";
    EXPECT_FALSE(parsed->chunks.empty()) << "Should have at least one chunk";

    // At least one chunk should have codecs
    bool foundCodecs = false;
    for (const auto& chunk : parsed->chunks) {
        if (!chunk.codecs.empty()) {
            foundCodecs = true;
        }
    }
    EXPECT_TRUE(foundCodecs) << "Should have codecs in at least one chunk";
}

} // namespace openzl

#endif // ZL_ALLOW_INTROSPECTION
