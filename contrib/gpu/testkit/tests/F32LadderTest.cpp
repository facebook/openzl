// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "contrib/gpu/testkit/f32_ladder.h"
#include "contrib/gpu/testkit/nimble_workloads.h"
#include "contrib/gpu/testkit/tests/f32_ladder_test_util.h"
#include "openzl/openzl.hpp"

namespace openzl::gpu::testkit {
namespace {

// Returns the codec IDs in a signature. It skips the conversion codecs, which
// only change a stream's type: convert_struct_to_serial 6,
// convert_struct_to_num 7, convert_num_to_struct 8, convert_serial_to_num 9,
// convert_num_to_serial 10.
std::set<std::string> codecs(const std::string& signature)
{
    static const std::set<std::string> kConversions = {
        "6", "7", "8", "9", "10"
    };
    std::set<std::string> ids;
    size_t start = 0;
    for (size_t i = 0; i < signature.size(); ++i) {
        const char c = signature[i];
        if (c == '(') {
            const std::string kind = signature.substr(start, i - start);
            const std::string id   = kind.substr(0, kind.find(':'));
            if (kConversions.count(id) == 0) {
                ids.insert(id);
            }
        }
        if (c == '(' || c == ',' || c == '|' || c == ';' || c == ')') {
            start = i + 1;
        }
    }
    return ids;
}

TEST(F32LadderTest, hasTwelveStepsAndUniquelyNamedWorkloads)
{
    std::set<std::string> names;
    for (const auto* workloads : { &f32Ladder(), &f32ExtraWorkloads() }) {
        for (const LadderStep& s : *workloads) {
            names.insert(s.name);
            EXPECT_EQ(s.name.rfind("f32/", 0), 0U) << s.name;
            EXPECT_EQ(s.spec.type, NimbleNumericType::F32) << s.name;
        }
    }
    EXPECT_EQ(f32Ladder().size(), 12U);
    EXPECT_EQ(names.size(), f32Ladder().size() + f32ExtraWorkloads().size());
}

TEST(F32LadderTest, frameSizesArePowersOfTwo)
{
    for (const auto* workloads : { &f32Ladder(), &f32ExtraWorkloads() }) {
        for (const LadderStep& s : *workloads) {
            EXPECT_EQ(s.frameBytes & (s.frameBytes - 1), 0U) << s.name;
        }
    }
}

// The codecs each step adds, from the ladder's design (codec IDs from the
// standard transform table): tokenize 37, zstd 22, field_lz 24,
// transpose_split 4, bitpack_int 28, huffman_v2 50, constant_serial 44,
// delta 1, quantize_offsets 25, bitpack_serial 27, quantize_lengths 26,
// flatpack 29, fse_ncount 52, fse_v2 49, splitn 40, tokenize_fixed 36,
// huffman_struct_v2 51, splitn_struct 47, range_pack 35.
TEST(F32LadderTest, eachStepAddsAtMostTwoCodecs)
{
    const std::vector<std::set<std::string>> added = {
        { "37", "22" }, { "24", "4" },  { "28", "50" }, { "44", "1" },
        { "25" },       { "27", "26" }, { "29" },       { "52", "49" },
        { "40", "36" }, { "51" },       { "47" },       { "35" },
    };
    std::set<std::string> seen;
    for (size_t i = 0; i < f32Ladder().size(); ++i) {
        std::set<std::string> fresh;
        for (const std::string& id : codecs(f32Ladder()[i].graph)) {
            if (seen.insert(id).second) {
                fresh.insert(id);
            }
        }
        EXPECT_EQ(fresh, added.at(i)) << f32Ladder()[i].name;
    }
    EXPECT_EQ(seen.size(), 19U);
}

TEST(F32LadderTest, extraWorkloadsNeedOnlyLadderCodecs)
{
    std::set<std::string> ladderCodecs;
    for (const LadderStep& s : f32Ladder()) {
        const std::set<std::string> ids = codecs(s.graph);
        ladderCodecs.insert(ids.begin(), ids.end());
    }
    for (const LadderStep& s : f32ExtraWorkloads()) {
        for (const std::string& id : codecs(s.graph)) {
            EXPECT_EQ(ladderCodecs.count(id), 1U) << s.name << " codec " << id;
        }
    }
}

class F32LadderStepTest : public ::testing::TestWithParam<const LadderStep*> {};

TEST_P(F32LadderStepTest, framesKeepTheirGraph)
{
    const LadderStep& s = *GetParam();
    expectGraphOnEveryFrame(
            s, [&](const std::string& raw) { return compressStep(s, raw); });
}

INSTANTIATE_TEST_SUITE_P(
        Steps,
        F32LadderStepTest,
        ::testing::ValuesIn(stepsOf(f32Ladder())),
        stepTestName);

INSTANTIATE_TEST_SUITE_P(
        ExtraWorkloads,
        F32LadderStepTest,
        ::testing::ValuesIn(stepsOf(f32ExtraWorkloads())),
        stepTestName);

TEST(F32LadderTest, framesRoundTrip)
{
    for (const auto* ladder : { &f32Ladder(), &f32ExtraWorkloads() }) {
        for (const LadderStep& s : *ladder) {
            if (s.frameBytes >= kLargeFrameBytes) {
                continue;
            }
            const std::string raw        = generateFrame(s.spec, s.frameBytes);
            const std::string compressed = compressStep(s, raw);
            EXPECT_EQ(openzl::DCtx{}.decompressSerial(compressed), raw)
                    << s.name;
        }
    }
}

} // namespace
} // namespace openzl::gpu::testkit
