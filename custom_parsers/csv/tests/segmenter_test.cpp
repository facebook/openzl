// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "custom_parsers/csv/csv_profile.h"
#include "openzl/openzl.hpp"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

#include "openzl/cpp/CompressIntrospectionHooks.hpp"

using namespace ::testing;

namespace openzl::custom_parsers {
class CsvSegmenterUnitTest : public ::testing::Test {
   public:
    void SetUp() override
    {
        auto gid = ZL_createGraph_genericCSVCompressorWithOptions(
                compressor_.get(), kChunkSize, true, ',', true);
        compressor_.selectStartingGraph(gid);
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.refCompressor(compressor_);
    }

    void roundtrip(std::string_view input)
    {
        std::string compressed(10000, '\0');
        auto csize = cctx_.compressSerial(compressed, input);
        compressed.resize(csize);
        auto regen = dctx_.decompressSerial(compressed);
        ASSERT_EQ(regen, input);
    }

   protected:
    const size_t kChunkSize = 500;
    CCtx cctx_{};
    DCtx dctx_{};
    Compressor compressor_{};
};

class CsvSegmentTestingReaderHook : public CompressIntrospectionHooks {
   public:
    explicit CsvSegmentTestingReaderHook(std::string_view targetName)
            : CompressIntrospectionHooks(), targetName_(targetName)
    {
    }

    void on_migraphEncode_start(
            ZL_Graph* gctx,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) override
    {
        std::string graphName = ZL_Compressor_Graph_getName(compressor, gid);
        if (graphName.size() == 0 || graphName != targetName_) {
            return;
        }
        EXPECT_EQ(nbInputs, 1);
        auto input = ZL_Edge_getData(inputs[0]);
        inputs_.emplace_back(
                (const char*)ZL_Input_ptr(input), ZL_Input_numElts(input));
    }

    std::vector<std::string> getInputs() const
    {
        return inputs_;
    }

   private:
    std::string_view targetName_;
    std::vector<std::string> inputs_;
};

TEST_F(CsvSegmenterUnitTest, EmptyInputRoundTrip)
{
    roundtrip("");
}

TEST_F(CsvSegmenterUnitTest, FixedCsvInputRoundTrip)
{
    constexpr std::string_view str =
            R"(H,2019GQ0000088,6,02600,3,01,1195583,1207712,0,1,3,,,,,,,,,,,,,1,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
H,2019GQ0000096,6,00700,3,01,1195583,1207712,0,1,2,,,,,,,,,,,,,2,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
H,2019GQ0000153,6,00800,3,01,1195583,1207712,0,1,3,,,,,,,,,,,,,2,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
)";
    roundtrip(str);
}

TEST_F(CsvSegmenterUnitTest, SegmenterGraphIsSerializable)
{
    auto serialized = compressor_.serialize();
    Compressor newCompressor;
    // Make it so that the graph ID is different from originally
    newCompressor.parameterizeGraph(ZL_GRAPH_CONSTANT, {});
    ZL_createGraph_genericCSVCompressor(newCompressor.get());
    newCompressor.deserialize(serialized);
}

TEST_F(CsvSegmenterUnitTest, SegmenterDividesInputAsExpected)
{
    constexpr std::string_view str =
            R"(H,2019GQ0000088,6,02600,3,01,1195583,1207712,0,1,3,,,,,,,,,,,,,1,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
H,2019GQ0000096,6,00700,3,01,1195583,1207712,0,1,2,,,,,,,,,,,,,2,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
H,2019GQ0000153,6,00800,3,01,1195583,1207712,0,1,3,,,,,,,,,,,,,2,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
)";
    constexpr std::string_view chunk1 =
            R"(H,2019GQ0000088,6,02600,3,01,1195583,1207712,0,1,3,,,,,,,,,,,,,1,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
H,2019GQ0000096,6,00700,3,01,1195583,1207712,0,1,2,,,,,,,,,,,,,2,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
)";
    constexpr std::string_view chunk2 =
            R"(H,2019GQ0000153,6,00800,3,01,1195583,1207712,0,1,3,,,,,,,,,,,,,2,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
)";
    // We want to read what is passed in the csv parser. The name happens to
    // have suffix #12 based on registration ordering.
    CsvSegmentTestingReaderHook hook("CSV Parser#12");
    ZL_CompressIntrospectionHooks* rawHooks = hook.getRawHooks();
    openzl::unwrap(
            ZL_CCtx_attachIntrospectionHooks(cctx_.get(), rawHooks),
            "Failed to attach introspection hooks",
            cctx_.get());
    std::string compressed(10000, '\0');
    cctx_.compressSerial(compressed, str);
    auto chunks = hook.getInputs();
    EXPECT_EQ(chunks[0], chunk1);
    EXPECT_EQ(chunks[1], chunk2);
}

} // namespace openzl::custom_parsers
