// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/e2e/e2e_parse.h"

#include <random>

#include "benchmark/benchmark_data.h"
#include "benchmark/e2e/e2e_bench.h"
#include "benchmark/e2e/e2e_compressor.h"
#include "benchmark/e2e/e2e_fieldlz.h"
#include "custom_transforms/parse/decode_parse.h"
#include "custom_transforms/parse/encode_parse.h"
#include "openzl/zl_opaque_types.h"

#if ZL_IS_FBCODE

#    include "custom_transforms/parse/tests/parse_test_data.h"

namespace zstrong::bench::e2e::parse {

namespace {
using openzl::tests::parse::Type;

class ParseCompressor : public ZstrongStringCompressor {
   public:
    explicit ParseCompressor(Type type, ZL_SetStringLensInstructions fieldSizes)
            : ZstrongStringCompressor(fieldSizes), type_(type)
    {
    }

    std::string name() override
    {
        return fmt::format(
                "Parse{}", type_ == Type::Float64 ? "Float64" : "Int64");
    }

    ZL_GraphID configureStringGraph(ZL_Compressor* cgraph) override
    {
        auto const node = type_ == Type::Float64
                ? ZS2_Compressor_registerParseFloat64(cgraph, 0)
                : ZS2_Compressor_registerParseInt64(cgraph, 1);
        std::vector<ZL_GraphID> successors(3, ZL_GRAPH_STORE);
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph, node, successors.data(), successors.size());
    }

    void registerDTransforms(ZL_DCtx* dctx) override
    {
        ZL_REQUIRE_SUCCESS(ZS2_DCtx_registerParseFloat64(dctx, 0));
        ZL_REQUIRE_SUCCESS(ZS2_DCtx_registerParseInt64(dctx, 1));
    }

   private:
    Type type_;
};

void registerBenchmark(size_t size, Type type)
{
    auto data                  = openzl::tests::parse::genData(size, type);
    auto [content, fieldSizes] = openzl::tests::parse::flatten(data);
    auto corpus                = std::make_shared<ArbitraryStringData>(
            std::move(content), std::move(fieldSizes));
    auto compressor =
            std::make_shared<ParseCompressor>(type, corpus->getFieldSizes());
    E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
}

} // namespace

void registerBenchmarks()
{
    registerBenchmark(100 * 1024, Type::Int64);
    registerBenchmark(100 * 1024, Type::Float64);
}

} // namespace zstrong::bench::e2e::parse

#else

namespace zstrong::bench::e2e::parse {

void registerBenchmarks() {}

} // namespace zstrong::bench::e2e::parse

#endif
