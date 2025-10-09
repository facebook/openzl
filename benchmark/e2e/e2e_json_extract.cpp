// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/e2e/e2e_json_extract.h"

#include <random>

#include "benchmark/benchmark_data.h"
#include "benchmark/e2e/e2e_bench.h"
#include "benchmark/e2e/e2e_compressor.h"
#include "custom_transforms/json_extract/decode_json_extract.h"
#include "custom_transforms/json_extract/encode_json_extract.h"
#include "openzl/zl_config.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_dtransform.h"

#if ZL_HAVE_FBCODE

#    include "custom_transforms/json_extract/tests/json_extract_test_data.h"

namespace zstrong::bench::e2e::json_extract {
namespace {

class JsonExtractCompressor : public ZstrongCompressor {
   public:
    std::string name() override
    {
        return "JsonExtract";
    }

    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
        auto const node = ZL_Compressor_registerJsonExtract(cgraph, 0);
        std::vector<ZL_GraphID> successors(4, ZL_GRAPH_STORE);
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph, node, successors.data(), successors.size());
    }

    void registerDTransforms(ZL_DCtx* dctx) override
    {
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerJsonExtract(dctx, 0));
    }
};

void registerBenchmark(size_t size)
{
    auto corpus = std::make_shared<ArbitrarySerializedData>(
            tests::genJsonLikeData(size));
    auto compressor = std::make_shared<JsonExtractCompressor>();
    E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
}

} // namespace

void registerBenchmarks()
{
    registerBenchmark(100 * 1024);
}

} // namespace zstrong::bench::e2e::json_extract

#else

namespace zstrong::bench::e2e::json_extract {

void registerBenchmarks() {}

} // namespace zstrong::bench::e2e::json_extract

#endif
