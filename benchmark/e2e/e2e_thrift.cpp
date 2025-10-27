// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/e2e/e2e_thrift.h"
#include <random>
#include "openzl/zl_config.h"

#if ZL_HAVE_FBCODE

#    include <sstream>
#    include <typeinfo>

#    include <folly/Demangle.h>

#    include "benchmark/benchmark_data.h"
#    include "benchmark/benchmark_data_utils.h"
#    include "benchmark/benchmark_testcase.h"
#    include "benchmark/e2e/e2e_bench.h"
#    include "benchmark/e2e/e2e_compressor.h"
#    include "benchmark/e2e/e2e_zstrong_utils.h"
#    include "custom_transforms/thrift/parse_config.h"
#    include "custom_transforms/thrift/tests/util.h"
#    include "custom_transforms/thrift/thrift_parsers.h"

using namespace zstrong::bench::utils;
using namespace zstrong::thrift;

namespace zstrong::bench::e2e {
namespace thrift {

namespace {
struct ThriftTestCase {
    std::string name;
    std::string config;
    std::shared_ptr<BenchmarkData> compactData;
    std::shared_ptr<BenchmarkData> binaryData;
};

class ThriftCompactCompressor : public ZstrongCompressor {
   private:
    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
        // Build the conversion and FieldLz graphs
        const auto cleanCompactNodeId =
                registerCompactTransform(cgraph, kThriftCompactConfigurable);
        const auto compactNodeId = cloneThriftNodeWithLocalParams(
                cgraph, cleanCompactNodeId, config_);

        std::vector<ZL_GraphID> thriftSuccessors{
            thriftCompactConfigurableSplitter.gd.nbSingletons
                    + thriftCompactConfigurableSplitter.gd.nbVOs,
            ZL_GRAPH_STORE
        };

        ZL_GraphID const graphId = ZL_Compressor_registerStaticGraph_fromNode(
                cgraph,
                compactNodeId,
                thriftSuccessors.data(),
                thriftSuccessors.size());

        return graphId;
    }

    void registerDTransforms(ZL_DCtx* dctx) override
    {
        ZL_REQUIRE_SUCCESS(
                registerCompactTransform(dctx, kThriftCompactConfigurable));
    }

   public:
    explicit ThriftCompactCompressor(const ThriftTestCase& testCase)
            : ZstrongCompressor(),
              config_(testCase.config),
              name_(testCase.name)
    {
    }

    virtual std::string name() override
    {
        return fmt::format("ThriftCompact_{}", name_);
    }

   private:
    std::string config_;
    std::string name_;
};

class ThriftBinaryCompressor : public ZstrongCompressor {
   private:
    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
        // Build the conversion and FieldLz graphs
        const auto cleanBinaryNodeId =
                registerBinaryTransform(cgraph, kThriftBinaryConfigurable);
        const auto binaryNodeId = cloneThriftNodeWithLocalParams(
                cgraph, cleanBinaryNodeId, config_);

        std::vector<ZL_GraphID> thriftSuccessors{
            thriftBinaryConfigurableSplitter.gd.nbSingletons
                    + thriftBinaryConfigurableSplitter.gd.nbVOs,
            ZL_GRAPH_STORE
        };

        ZL_GraphID const graphId = ZL_Compressor_registerStaticGraph_fromNode(
                cgraph,
                binaryNodeId,
                thriftSuccessors.data(),
                thriftSuccessors.size());

        return graphId;
    }

    void registerDTransforms(ZL_DCtx* dctx) override
    {
        ZL_REQUIRE_SUCCESS(
                registerBinaryTransform(dctx, kThriftBinaryConfigurable));
    }

   public:
    explicit ThriftBinaryCompressor(const ThriftTestCase& testCase)
            : ZstrongCompressor(),
              config_(testCase.config),
              name_(testCase.name)
    {
    }

    virtual std::string name() override
    {
        return fmt::format("ThriftBinary_{}", name_);
    }

   private:
    std::string config_;
    std::string name_;
};

template <typename T>
std::vector<T> buildAlphabet(size_t minLog, size_t maxLog)
{
    static_assert(std::is_integral_v<T>, "Not useful for floating-point types");
    ZL_REQUIRE(minLog < maxLog);
    ZL_REQUIRE((maxLog >> 3) < sizeof(T));
    std::vector<T> alphabet;
    for (size_t i = minLog; i <= maxLog; i++) {
        alphabet.push_back((T)(1 << i));
    }
    return alphabet;
}

template <typename T>
std::vector<T> buildRandomList(size_t size)
{
    std::vector<T> result;
    std::mt19937 gen(0xdeadbeef);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(::zstrong::thrift::tests::generate<T>(gen));
    }
    return result;
}

template <typename T, typename U>
std::unordered_map<T, U> buildRandomMap(size_t size)
{
    static_assert(std::is_integral_v<T>);
    ZL_REQUIRE(size < std::numeric_limits<T>::max());
    std::vector<T> keys(size);
    // Not the most interesting distribution for keys, could be improved along
    // the lines of buildAlphabet() above
    std::iota(keys.begin(), keys.end(), 0);
    auto const values = buildRandomList<U>(size);
    std::unordered_map<T, U> map;
    for (size_t i = 0; i < size; i++) {
        map.emplace(keys.at(i), values.at(i));
    }
    ZL_REQUIRE(map.size() == size);
    return map;
}

template <typename T>
ThriftTestCase buildBigListTestCase(size_t targetSizeBytes)
{
    ThriftTestCase testCase;
    testCase.name =
            fmt::format("BigList<{}>_TypeSplit", folly::demangle(typeid(T)));
    testCase.config = EncoderConfig({}, {}, TType::T_LIST).serialize();

    std::vector<T> list = buildRandomList<T>(targetSizeBytes / sizeof(T));
    auto const str_compact =
            apache::thrift::CompactSerializer::serialize<std::string>(list);
    auto const str_binary =
            apache::thrift::BinarySerializer::serialize<std::string>(list);
    testCase.compactData =
            std::make_shared<ArbitrarySerializedData>(str_compact);
    testCase.binaryData = std::make_shared<ArbitrarySerializedData>(str_binary);

    return testCase;
}

template <typename T>
ThriftTestCase buildManySmallListsTestCase(size_t targetSizeBytes)
{
    ThriftTestCase testCase;
    testCase.name = fmt::format(
            "ManySmallLists<{}>_TypeSplit", folly::demangle(typeid(T)));
    testCase.config = EncoderConfig({}, {}, TType::T_LIST).serialize();

    std::mt19937 gen(0xfaceb00c);
    std::bernoulli_distribution emptyDist;
    std::uniform_int_distribution<size_t> listSizeDist(1, 4);
    std::stringstream ss_compact;
    std::stringstream ss_binary;
    size_t compactBytes = 0;
    size_t binaryBytes  = 0;
    while (true) {
        size_t const listSize = emptyDist(gen) ? 0 : listSizeDist(gen);
        const auto list       = buildRandomList<T>(listSize);
        auto const str_compact =
                apache::thrift::CompactSerializer::serialize<std::string>(list);
        auto const str_binary =
                apache::thrift::BinarySerializer::serialize<std::string>(list);

        ss_compact << str_compact;
        compactBytes += str_compact.size();
        ss_binary << str_binary;
        binaryBytes += str_binary.size();

        if (compactBytes >= targetSizeBytes || binaryBytes >= targetSizeBytes) {
            break;
        }
    }

    testCase.compactData =
            std::make_shared<ArbitrarySerializedData>(ss_compact.str());
    testCase.binaryData =
            std::make_shared<ArbitrarySerializedData>(ss_binary.str());
    return testCase;
}

template <typename T, typename U>
ThriftTestCase buildManySmallMapsTestCase(size_t targetSizeBytes)
{
    ThriftTestCase testCase;
    testCase.name = fmt::format(
            "ManySmallMaps<{},{}>_TypeSplit",
            folly::demangle(typeid(T)),
            folly::demangle(typeid(U)));
    testCase.config = EncoderConfig({}, {}, TType::T_MAP).serialize();

    std::mt19937 gen(0xfaceb00c);
    std::bernoulli_distribution emptyDist;
    std::uniform_int_distribution<size_t> mapSizeDist(1, 4);
    std::stringstream ss_compact;
    std::stringstream ss_binary;
    size_t compactBytes = 0;
    size_t binaryBytes  = 0;
    while (true) {
        size_t const mapSize = emptyDist(gen) ? 0 : mapSizeDist(gen);
        const auto map       = buildRandomMap<T, U>(mapSize);
        auto const str_compact =
                apache::thrift::CompactSerializer::serialize<std::string>(map);
        auto const str_binary =
                apache::thrift::BinarySerializer::serialize<std::string>(map);

        ss_compact << str_compact;
        compactBytes += str_compact.size();
        ss_binary << str_binary;
        binaryBytes += str_binary.size();

        if (compactBytes >= targetSizeBytes || binaryBytes >= targetSizeBytes) {
            break;
        }
    }

    testCase.compactData =
            std::make_shared<ArbitrarySerializedData>(ss_compact.str());
    testCase.binaryData =
            std::make_shared<ArbitrarySerializedData>(ss_binary.str());
    return testCase;
}

template <typename T, typename U>
ThriftTestCase buildBigMapTestCase(size_t targetSizeBytes)
{
    ThriftTestCase testCase;
    testCase.name = fmt::format(
            "BigMap<{},{}>_TypeSplit",
            folly::demangle(typeid(T)),
            folly::demangle(typeid(U)));
    testCase.config = EncoderConfig({}, {}, TType::T_MAP).serialize();

    std::unordered_map<T, U> map =
            buildRandomMap<T, U>(targetSizeBytes / (sizeof(T) + sizeof(U)));
    auto const str_compact =
            apache::thrift::CompactSerializer::serialize<std::string>(map);
    auto const str_binary =
            apache::thrift::BinarySerializer::serialize<std::string>(map);
    testCase.compactData =
            std::make_shared<ArbitrarySerializedData>(str_compact);
    testCase.binaryData = std::make_shared<ArbitrarySerializedData>(str_binary);

    return testCase;
}

std::vector<ThriftTestCase> buildTestCases()
{
    std::vector<ThriftTestCase> testCases;
    constexpr size_t targetSizeBytes = 1024 * 1024;

    // Random Thrift with an empty config
    {
        ThriftTestCase testCase;
        testCase.name   = "Random_TypeSplit";
        testCase.config = EncoderConfig({}, {}).serialize();

        std::mt19937 genCompact(0xfaceb00c);
        std::mt19937 genBinary(0xfaceb00c);
        std::stringstream ss_compact;
        std::stringstream ss_binary;
        size_t compactBytes = 0;
        size_t binaryBytes  = 0;
        while (true) {
            std::string const str_compact =
                    ::zstrong::thrift::tests::generateRandomThrift<
                            apache::thrift::CompactSerializer>(genCompact);
            std::string const str_binary =
                    ::zstrong::thrift::tests::generateRandomThrift<
                            apache::thrift::BinarySerializer>(genBinary);

            ss_compact << str_compact;
            compactBytes += str_compact.size();
            ss_binary << str_binary;
            binaryBytes += str_binary.size();

            if (compactBytes >= targetSizeBytes
                || binaryBytes >= targetSizeBytes) {
                break;
            }
        }

        testCase.compactData =
                std::make_shared<ArbitrarySerializedData>(ss_compact.str());
        testCase.binaryData =
                std::make_shared<ArbitrarySerializedData>(ss_binary.str());
        testCases.push_back(std::move(testCase));
    }

    // Large lists of various numeric types with an empty config
    {
        testCases.push_back(buildBigListTestCase<int16_t>(targetSizeBytes));
        testCases.push_back(buildBigListTestCase<int32_t>(targetSizeBytes));
        testCases.push_back(buildBigListTestCase<int64_t>(targetSizeBytes));
        testCases.push_back(buildBigListTestCase<float>(targetSizeBytes));
        testCases.push_back(buildBigListTestCase<double>(targetSizeBytes));
        testCases.push_back(buildBigListTestCase<std::string>(targetSizeBytes));
        testCases.push_back(
                buildBigListTestCase<
                        ::zstrong::thrift::tests::cpp2::PrimitiveTestStruct>(
                        targetSizeBytes));
    }

    // Small list batches of various numeric types with an empty config
    {
        testCases.push_back(
                buildManySmallListsTestCase<int16_t>(targetSizeBytes));
        testCases.push_back(
                buildManySmallListsTestCase<int32_t>(targetSizeBytes));
        testCases.push_back(
                buildManySmallListsTestCase<int64_t>(targetSizeBytes));
        testCases.push_back(
                buildManySmallListsTestCase<float>(targetSizeBytes));
        testCases.push_back(
                buildManySmallListsTestCase<double>(targetSizeBytes));
        testCases.push_back(
                buildManySmallListsTestCase<std::string>(targetSizeBytes));
    }

    // Large maps of various numeric types with an empty config
    {
        testCases.push_back(
                buildBigMapTestCase<int32_t, float>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int32_t, double>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int32_t, int32_t>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int32_t, int64_t>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int64_t, float>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int64_t, double>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int64_t, int32_t>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int64_t, int64_t>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<int64_t, std::string>(targetSizeBytes));
        testCases.push_back(
                buildBigMapTestCase<
                        int64_t,
                        ::zstrong::thrift::tests::cpp2::PrimitiveTestStruct>(
                        targetSizeBytes));
    }

    // Small maps of various numeric types with an empty config
    {
        testCases.push_back(
                buildManySmallMapsTestCase<int32_t, float>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int32_t, double>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int32_t, int32_t>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int32_t, int64_t>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int64_t, float>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int64_t, double>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int64_t, int32_t>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int64_t, int64_t>(targetSizeBytes));
        testCases.push_back(
                buildManySmallMapsTestCase<int64_t, std::string>(
                        targetSizeBytes));
    }

    return testCases;
}

} // namespace

void registerBenchmarks()
{
    std::vector<ThriftTestCase> const testCases = buildTestCases();

    for (const auto& testCase : testCases) {
        auto compact_compressor =
                std::make_shared<ThriftCompactCompressor>(testCase);
        E2EBenchmarkTestcase(compact_compressor, testCase.compactData)
                .registerBenchmarks();
        auto binary_compressor =
                std::make_shared<ThriftBinaryCompressor>(testCase);
        E2EBenchmarkTestcase(binary_compressor, testCase.binaryData)
                .registerBenchmarks();
    }
}
} // namespace thrift
} // namespace zstrong::bench::e2e

#else

namespace zstrong::bench::e2e::thrift {

void registerBenchmarks() {}

} // namespace zstrong::bench::e2e::thrift

#endif
