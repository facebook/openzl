// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <benchmark/benchmark.h>
#include <fmt/format.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "benchmark/benchmark_config.h"
#include "benchmark/benchmark_data.h"
#include "benchmark/benchmark_data_utils.h"
#include "benchmark/e2e/e2e_bench.h"
#include "benchmark/e2e/e2e_compressor.h"
#include "benchmark/e2e/e2e_fieldlz.h"
#include "benchmark/e2e/e2e_json_extract.h"
#include "benchmark/e2e/e2e_parse.h"
#include "benchmark/e2e/e2e_sao.h"
#include "benchmark/e2e/e2e_splitByStruct.h"
#include "benchmark/e2e/e2e_thrift.h"
#include "openzl/codecs/dispatch_string/decode_dispatch_string_binding.h"
#include "openzl/codecs/dispatch_string/encode_dispatch_string_binding.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_public_nodes.h"

namespace zstrong::bench::e2e {

void E2EBenchmarkTestcase::registerBenchmarks()
{
    auto CompressBM = [compressor = _compressor,
                       data       = _data](benchmark::State& state) mutable {
        compressor->benchCompression(state, data->data());
    };
    RegisterBenchmark(
            fmt::format(
                    "E2E / {} / {} / Compress",
                    _compressor->name(),
                    _data->name()),
            CompressBM);

    auto DecompressBM = [compressor = _compressor,
                         data       = _data](benchmark::State& state) mutable {
        compressor->benchDecompression(state, data->data());
    };
    RegisterBenchmark(
            fmt::format(
                    "E2E / {} / {} / Decompress",
                    _compressor->name(),
                    _data->name()),
            DecompressBM);
}

static void registerConstantBenchmarks()
{
    std::vector<std::shared_ptr<BenchmarkData>> corpora = {
        std::make_shared<ConstantData>(100000, 1),
        std::make_shared<ConstantData>(100000, 2),
        std::make_shared<ConstantData>(100000, 3),
        std::make_shared<ConstantData>(100000, 4),
        std::make_shared<ConstantData>(100000, 5),
        std::make_shared<ConstantData>(100000, 8),
        std::make_shared<ConstantData>(100000, 10),
        std::make_shared<ConstantData>(100000, 16),
        std::make_shared<ConstantData>(100000, 20),
    };
    for (auto const& corpus : corpora) {
        auto compressor = std::make_shared<ZstrongCompressorStandardNode>(
                ZL_NODE_CONSTANT_FIXED, "Constant", corpus->width());
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
    }
}

static void registerTransposeBenchmarks()
{
    std::vector<std::tuple<std::string, ZL_NodeID>> nodes = {
        { "TransposeSplit", ZL_NODE_TRANSPOSE_SPLIT },
    };
    std::vector<std::shared_ptr<BenchmarkData>> corpora = {
        std::make_shared<FixedSizeData>(1000, 2),
        std::make_shared<FixedSizeData>(10000, 2),
        std::make_shared<FixedSizeData>(100000, 2),
        std::make_shared<FixedSizeData>(1000, 4),
        std::make_shared<FixedSizeData>(10000, 4),
        std::make_shared<FixedSizeData>(100000, 4),
        std::make_shared<FixedSizeData>(1000, 5),
        std::make_shared<FixedSizeData>(10000, 5),
        std::make_shared<FixedSizeData>(100000, 5),
        std::make_shared<FixedSizeData>(1000, 8),
        std::make_shared<FixedSizeData>(10000, 8),
        std::make_shared<FixedSizeData>(100000, 8),
        std::make_shared<FixedSizeData>(1000, 10),
        std::make_shared<FixedSizeData>(10000, 10),
        std::make_shared<FixedSizeData>(100000, 10),
        std::make_shared<FixedSizeData>(1000, 15),
        std::make_shared<FixedSizeData>(10000, 15),
        std::make_shared<FixedSizeData>(100000, 15),
    };
    for (auto [name, nid] : nodes) {
        for (auto corpus : corpora) {
            auto compressor = std::make_shared<ZstrongCompressorStandardNode>(
                    nid, name, corpus->width());
            E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
        }
    }
}

static void registerPrefixBenchmarks()
{
    std::vector<std::shared_ptr<VariableSizeData>> corpora = {
        std::make_shared<VariableSizeData>(true, 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(true, 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(true, 1024, 10, 20, 4),
        std::make_shared<VariableSizeData>(true, 10 * 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(true, 10 * 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(true, 10 * 1024, 10, 20, 4),
        std::make_shared<VariableSizeData>(true, 100 * 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(true, 100 * 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(true, 100 * 1024, 10, 20, 4),
    };
    for (auto corpus : corpora) {
        auto compressor = std::make_shared<ZstrongStringStandardNodeCompressor>(
                ZL_NODE_PREFIX, "Prefix", corpus->getFieldSizes());
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
    }
}

static void registerTokenizeBenchmarks()
{
    std::vector<std::tuple<std::string, ZL_NodeID>> nodes = {
        { "Tokenize", ZL_NODE_TOKENIZE },
        { "TokenizeSorted", ZL_NODE_TOKENIZE_SORTED },
    };
    std::vector<std::shared_ptr<BenchmarkData>> corpuses = {
        // 1-byte index
        std::make_shared<UniformDistributionData<uint8_t>>(100 * 1024, 100),
        std::make_shared<UniformDistributionData<uint16_t>>(16, 100),
        std::make_shared<UniformDistributionData<uint16_t>>(128, 100),
        std::make_shared<UniformDistributionData<uint16_t>>(1 * 1024, 100),
        std::make_shared<UniformDistributionData<uint16_t>>(10 * 1024, 100),
        std::make_shared<UniformDistributionData<uint16_t>>(100 * 1024, 100),
        std::make_shared<UniformDistributionData<uint16_t>>(1024 * 1024, 100),
        std::make_shared<UniformDistributionData<uint32_t>>(100 * 1024, 100),
        std::make_shared<UniformDistributionData<uint64_t>>(100 * 1024, 100),

        // 2-byte index
        std::make_shared<UniformDistributionData<uint16_t>>(100 * 1024, 1000),
        std::make_shared<UniformDistributionData<uint32_t>>(100 * 1024, 1000),
        std::make_shared<UniformDistributionData<uint64_t>>(100 * 1024, 1000),
        std::make_shared<UniformDistributionData<uint64_t>>(100 * 1024, 10000),

        // 4-byte index
        std::make_shared<UniformDistributionData<uint32_t>>(
                10 * 1024 * 1024, 100000),
        std::make_shared<UniformDistributionData<uint64_t>>(
                10 * 1024 * 1024, 100000),
        std::make_shared<UniformDistributionData<uint64_t>>(100 * 1024, 100000),
    };
    for (auto [name, nid] : nodes) {
        for (auto corpus : corpuses) {
            auto compressor = std::make_shared<ZstrongCompressorStandardNode>(
                    nid, name, corpus->width());
            E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
        }
    }

    std::vector<std::tuple<std::string, ZL_NodeID>> stringNodes = {
        { "TokenizeString", ZL_NODE_TOKENIZE_STRING },
        { "TokenizeStringSorted", ZL_NODE_TOKENIZE_STRING_SORTED },
    };
    std::vector<std::shared_ptr<VariableSizeData>> corpora = {
        std::make_shared<VariableSizeData>(false, 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(false, 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(false, 1024, 10, 20, 4),
        std::make_shared<VariableSizeData>(false, 10 * 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(false, 10 * 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(false, 10 * 1024, 10, 20, 4),
        std::make_shared<VariableSizeData>(false, 100 * 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(false, 100 * 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(false, 100 * 1024, 10, 20, 4),
    };
    for (auto [name, nid] : stringNodes) {
        for (auto corpus : corpora) {
            auto compressor =
                    std::make_shared<ZstrongStringStandardNodeCompressor>(
                            nid, name, corpus->getFieldSizes());
            E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
        }
    }
}

template <typename Int>
static void registerBitpackBenchmark(size_t nbElts)
{
    auto makeCorpus = [&](size_t nbBits) {
        assert(nbBits <= sizeof(Int) * 8);
        static_assert(std::is_unsigned_v<Int>);
        Int max = Int(-1);
        if (nbBits < sizeof(Int) * 8) {
            max = Int(Int(1) << nbBits) - 1;
        }
        return std::make_shared<UniformDistributionData<Int>>(
                nbElts, std::nullopt, 0, max);
    };

    std::vector<std::shared_ptr<BenchmarkData>> corpora;

    corpora.push_back(makeCorpus(1));
    corpora.push_back(makeCorpus(7));
    corpora.push_back(makeCorpus(8));
    if (sizeof(Int) > 1) {
        corpora.push_back(makeCorpus(9));
        corpora.push_back(makeCorpus(12));
        corpora.push_back(makeCorpus(15));
        corpora.push_back(makeCorpus(16));
    }
    if (sizeof(Int) > 2) {
        corpora.push_back(makeCorpus(17));
        corpora.push_back(makeCorpus(24));
        corpora.push_back(makeCorpus(31));
        corpora.push_back(makeCorpus(32));
    }
    if (sizeof(Int) > 4) {
        corpora.push_back(makeCorpus(33));
        corpora.push_back(makeCorpus(40));
        corpora.push_back(makeCorpus(48));
        corpora.push_back(makeCorpus(50));
        corpora.push_back(makeCorpus(56));
        corpora.push_back(makeCorpus(63));
        corpora.push_back(makeCorpus(64));
    }

    for (auto corpus : corpora) {
        auto compressor = std::make_shared<ZstrongCompressorStandardNode>(
                ZL_NODE_BITPACK_INT, "bitpack", corpus->width());
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
    }
}

static void registerBitpackBenchmarks()
{
    registerBitpackBenchmark<uint8_t>(10000);
    registerBitpackBenchmark<uint16_t>(10000);
    registerBitpackBenchmark<uint32_t>(10000);
    registerBitpackBenchmark<uint64_t>(10000);
}

/*
 * DISPATCH BENCHMARKS
 */
namespace {

// Register a benchmark for dispatch transform that dispatches fixed sized
// fields, the field type is the first argument and the number of tags is
// the second template argument.
template <typename T, uint8_t maxTags>
static void registerFixedSizeDispatchBenchmark()
{
    auto dispatchNodeGen = [](ZL_Compressor* cgraph) {
        auto parser = [](ZL_DispatchState* ds, const ZL_Input* in) {
            ZL_REQUIRE_EQ(ZL_Input_numElts(in) % sizeof(T), 0);
            size_t const nbElts    = ZL_Input_numElts(in) / sizeof(T);
            T const* input         = (T const*)ZL_Input_ptr(in);
            size_t* const segSizes = (size_t*)ZL_DispatchState_malloc(
                    ds, nbElts * sizeof(size_t));
            unsigned* const tags = (unsigned*)ZL_DispatchState_malloc(
                    ds, nbElts * sizeof(unsigned));
            for (size_t i = 0; i < nbElts; ++i) {
                segSizes[i] = sizeof(T);
                tags[i]     = (unsigned)input[i];
            }
            return (ZL_DispatchInstructions){ segSizes, tags, nbElts, maxTags };
        };

        return ZL_Compressor_registerDispatchNode(cgraph, parser, nullptr);
    };

    auto compressor = std::make_shared<ZstrongCompressorNode>(
            "DispatchFixedSizeSegments", 1, dispatchNodeGen);
    std::shared_ptr<BenchmarkData> corpus =
            std::make_shared<UniformDistributionData<T>>(
                    10240, std::nullopt, 0, maxTags - 1);
    E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
}

static void registerVaryingSizeDispatchBenchmark()
{
    auto dispatchNodeGen = [](ZL_Compressor* cgraph) {
        auto parser = [](ZL_DispatchState* ds, const ZL_Input* in) {
            size_t const nbElts    = ZL_Input_numElts(in);
            uint8_t const* input   = (uint8_t const*)ZL_Input_ptr(in);
            size_t* const segSizes = (size_t*)ZL_DispatchState_malloc(
                    ds, nbElts * sizeof(size_t));
            unsigned* const tags = (unsigned*)ZL_DispatchState_malloc(
                    ds, nbElts * sizeof(unsigned));

            size_t i = 0;
            size_t s = 0;
            while (i < nbElts) {
                const size_t segSize = std::min(
                        (size_t)(nbElts - i), ((size_t)input[i] >> 3) + 1);
                segSizes[s] = segSize;
                tags[s]     = input[i] & 7;
                s++;
                i += segSize;
            }
            return (ZL_DispatchInstructions){ segSizes, tags, s, 8 };
        };

        return ZL_Compressor_registerDispatchNode(cgraph, parser, nullptr);
    };

    auto compressor = std::make_shared<ZstrongCompressorNode>(
            "DispatchVaryingSizedSegments", 1, dispatchNodeGen);
    std::shared_ptr<BenchmarkData> corpus =
            std::make_shared<UniformDistributionData<uint8_t>>(
                    10240, std::nullopt);
    E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
}

static void registerDispatchBenchmarks()
{
    registerFixedSizeDispatchBenchmark<uint8_t, 8>();
    registerFixedSizeDispatchBenchmark<uint8_t, 254>();
    registerFixedSizeDispatchBenchmark<uint64_t, 8>();
    registerVaryingSizeDispatchBenchmark();
}

void registerEntropyBenchmarks()
{
    std::vector<std::tuple<std::string, ZL_GraphID>> graphs = {
        { "FSE", ZL_GRAPH_FSE },
        { "Huffman", ZL_GRAPH_HUFFMAN },
        { "Entropy", ZL_GRAPH_ENTROPY },
    };
    std::vector<std::shared_ptr<BenchmarkData>> corpuses = {
        std::make_shared<MostlyConstantData>(),
        std::make_shared<UniformDistributionData<uint8_t>>(10240, 100),
        std::make_shared<UniformDistributionData<uint16_t>>(10240, 100),
        std::make_shared<NormalDistributionData<uint8_t>>(128, 10, 10240),
        std::make_shared<NormalDistributionData<uint8_t>>(128, 1, 10240),
        std::make_shared<NormalDistributionData<uint32_t>>(128, 10, 10240),
        std::make_shared<NormalDistributionData<uint32_t>>(
                UINT32_MAX / 2, 1024, 10240),
        std::make_shared<ConstantData>(1000, 1),
        std::make_shared<UniformDistributionData<uint8_t>>(100001, 100),
        std::make_shared<NormalDistributionData<uint8_t>>(128, 10, 100001),
        std::make_shared<NormalDistributionData<uint8_t>>(128, 1, 100001),
    };
    for (auto [name, gid] : graphs) {
        auto compressor =
                std::make_shared<ZstrongCompressorStandard>(gid, name);
        for (auto corpus : corpuses) {
            E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
        }
    }
}

class Huffman2Compressor : public ZstrongCompressor {
   private:
    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
#ifdef ZL_GRAPH_HUFFMAN
        ZL_GraphID const huffman = ZL_GRAPH_HUFFMAN;
#else
        ZL_GraphID const huffman = ZL_GRAPH_HUFFMAN;
#endif
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph, ZL_NODE_CONVERT_SERIAL_TO_TOKEN2, huffman);
    }

   public:
    Huffman2Compressor()  = default;
    ~Huffman2Compressor() = default;

    std::string name() override
    {
        return "Huffman2";
    }
};

void registerHuffman2Benchmark()
{
    std::vector<std::shared_ptr<BenchmarkData>> corpuses = {
        std::make_shared<UniformDistributionData<uint16_t>>(
                10000, 500, 0, 1000),
        std::make_shared<UniformDistributionData<uint16_t>>(
                100001, 500, 0, 1000),
        std::make_shared<NormalDistributionData<uint16_t>>(500, 100, 10000),
        std::make_shared<NormalDistributionData<uint16_t>>(500, 100, 100001),
        std::make_shared<ConstantData>(1000, 2),

        std::make_shared<UniformDistributionData<uint16_t>>(
                100000, 255, 0, 255),
        std::make_shared<NormalDistributionData<uint16_t>>(50, 5, 100),

        std::make_shared<UniformDistributionData<uint16_t>>(
                100000, 255, 0, 30000),
        std::make_shared<UniformDistributionData<uint16_t>>(
                100000, 10, 0, 30000),
        std::make_shared<NormalDistributionData<uint16_t>>(10000, 5, 100),
        std::make_shared<NormalDistributionData<uint16_t>>(10000, 5, 10000),
    };
    auto compressor = std::make_shared<Huffman2Compressor>();
    for (auto corpus : corpuses) {
        ZL_REQUIRE_EQ(corpus->width(), 2);
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
    }
}
} // namespace

/* Benchmarks for the divide-by transform */
namespace {

class DivideByCompressor : public ZstrongCompressor {
   private:
    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
        ZL_NodeID node_divideBy;
        if (divisor_ != 0) {
            node_divideBy =
                    ZL_Compressor_registerDivideByNode(cgraph, divisor_);
        } else {
            node_divideBy = ZL_NodeID{ ZL_StandardNodeID_divide_by };
        }
        ZL_NodeID convert;
        switch (intWidth_) {
            case 1:
                convert = ZL_NODE_INTERPRET_AS_LE8;
                break;
            case 2:
                convert = ZL_NODE_INTERPRET_AS_LE16;
                break;
            case 4:
                convert = ZL_NODE_INTERPRET_AS_LE32;
                break;
            case 8:
                convert = ZL_NODE_INTERPRET_AS_LE64;
                break;
        }
        ZL_GraphID graphId = ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph, node_divideBy, ZL_GRAPH_STORE);
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph, convert, graphId);
    }

    size_t intWidth_;
    uint64_t divisor_;
    const std::string name_;

   public:
    DivideByCompressor(
            size_t intWidth,
            uint64_t divisor,
            const std::string& name)
            : intWidth_(intWidth), divisor_(divisor), name_(name)
    {
    }

    std::string name() override
    {
        return name_;
    }
};

static void registerDivideByGcdBenchmarks()
{
    std::vector<std::shared_ptr<BenchmarkData>> corpuses = {
        std::make_shared<CustomDistributionData<uint16_t>>(
                10000,
                [](size_t size, size_t seed) {
                    return zstrong::bench::utils::generateDivisableData<
                            uint16_t>(size, seed, 5);
                }),
        std::make_shared<CustomDistributionData<uint8_t>>(
                100000,
                [](size_t size, size_t seed) {
                    return zstrong::bench::utils::generateDivisableData<
                            uint8_t>(size, seed, 3);
                }),
        std::make_shared<CustomDistributionData<uint16_t>>(
                100000,
                [](size_t size, size_t seed) {
                    return zstrong::bench::utils::generateDivisableData<
                            uint16_t>(size, seed, 40);
                }),
        std::make_shared<CustomDistributionData<uint32_t>>(
                100000,
                [](size_t size, size_t seed) {
                    return zstrong::bench::utils::generateDivisableData<
                            uint32_t>(size, seed, 15);
                }),
        std::make_shared<CustomDistributionData<uint64_t>>(
                100000,
                [](size_t size, size_t seed) {
                    return zstrong::bench::utils::generateDivisableData<
                            uint64_t>(size, seed, 25);
                }),
    };
    std::vector<uint64_t> divisors = { 5, 3, 40, 15, 25 };
    for (size_t i = 0; i < corpuses.size(); ++i) {
        auto corpus     = corpuses[i];
        auto compressor = std::make_shared<DivideByCompressor>(
                corpus->width(), divisors[i], "DivideByKnown");
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
        auto gcdCompressor = std::make_shared<DivideByCompressor>(
                corpus->width(), 0, "DivideByGcd");
        E2EBenchmarkTestcase(gcdCompressor, corpus).registerBenchmarks();
    }
}
} // namespace

// Benchmark dispatchString (custom transform)
namespace {

class DispatchStringCompressor : public ZstrongStringCompressor {
   private:
    const int nbOuts_;
    const std::shared_ptr<UniformDistributionData<uint16_t>> rawIndices_;

    ZL_GraphID configureStringGraph(ZL_Compressor* cgraph) override
    {
        const uint16_t* indices =
                (const uint16_t*)((const void*)rawIndices_->data().data());
        auto dispatchStringNode = ZL_Compressor_registerDispatchStringNode(
                cgraph, nbOuts_, indices);
        const auto nbOutcomes =
                ZL_Compressor_Node_getNumOutcomes(cgraph, dispatchStringNode);
        std::vector<ZL_GraphID> children(nbOutcomes, ZL_GRAPH_STORE);
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph, dispatchStringNode, children.data(), children.size());
    }

   public:
    DispatchStringCompressor(
            ZL_SetStringLensInstructions fieldSizes,
            std::shared_ptr<UniformDistributionData<uint16_t>> indices,
            int nbOuts)
            : ZstrongStringCompressor(fieldSizes),
              nbOuts_(nbOuts),
              rawIndices_(indices)
    {
    }

    std::string name() override
    {
        return "DispatchString";
    }
};

static void registerDispatchStringBenchmarks()
{
    std::vector<std::shared_ptr<VariableSizeData>> corpora = {
        std::make_shared<VariableSizeData>(false, 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(false, 1024, 10, 20, 4),
        std::make_shared<VariableSizeData>(false, 100 * 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(false, 100 * 1024, 10, 20, 4),
    };
    std::vector<int> nbOuts = { 4, 4, 16, 16 };
    std::vector<std::shared_ptr<UniformDistributionData<uint16_t>>> indicesArr;
    for (size_t i = 0; i < corpora.size(); ++i) {
        auto indices = std::make_shared<UniformDistributionData<uint16_t>>(
                corpora[i]->getFieldSizes().nbStrings,
                std::nullopt,
                0,
                nbOuts[i] - 1);
        indicesArr.emplace_back(std::move(indices));
    }

    assert(corpora.size() == indicesArr.size());
    for (size_t i = 0; i < corpora.size(); ++i) {
        const auto corpus = corpora[i];
        auto compressor   = std::make_shared<DispatchStringCompressor>(
                corpus->getFieldSizes(), indicesArr[i], nbOuts[i]);
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
    }
}

} // anonymous namespace

static void registerMergeSortedBenchmarks()
{
    std::vector<std::shared_ptr<BenchmarkData>> corpora = {
        std::make_shared<SortedRunsData<uint32_t>>(8, 1000, 1600),
        std::make_shared<SortedRunsData<uint32_t>>(16, 1000, 1600),
        std::make_shared<SortedRunsData<uint32_t>>(32, 1000, 1600),
        std::make_shared<SortedRunsData<uint32_t>>(64, 1000, 1600)
    };
    for (auto const& corpus : corpora) {
        auto compressor = std::make_shared<ZstrongCompressorStandardNode>(
                ZL_NODE_MERGE_SORTED, "MergeSorted", corpus->width());
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
    }
}

void registerE2EBenchmarks()
{
    registerEntropyBenchmarks();
    registerHuffman2Benchmark();
    registerMergeSortedBenchmarks();
    registerConstantBenchmarks();
    registerTransposeBenchmarks();
    registerPrefixBenchmarks();
    registerTokenizeBenchmarks();
    registerDispatchBenchmarks();
    registerBitpackBenchmarks();
    registerDivideByGcdBenchmarks();
    registerDispatchStringBenchmarks();
    fieldlz::registerFieldLzBenchmarks();
    sao::registerSAOBenchmarks();
    splitByStruct::registerBenchmarks();
    thrift::registerBenchmarks();
    json_extract::registerBenchmarks();
    parse::registerBenchmarks();
}

} // namespace zstrong::bench::e2e
