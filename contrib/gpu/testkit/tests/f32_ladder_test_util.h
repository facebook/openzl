// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "contrib/gpu/testkit/f32_ladder.h"
#include "contrib/gpu/testkit/frame_verifier.h"
#include "contrib/gpu/testkit/nimble_workloads.h"

// Helpers that the ladder's graph tests share.

namespace openzl::gpu::testkit {

// The graph test checks small frames for seeds 1 to 10, frames 0 to 9. A
// frame of several MiB takes seconds, so the steps of 1 MiB and more check
// seeds 1 to 4, frames 0 and 1.
constexpr size_t kLargeFrameBytes = size_t{ 1 } << 20;

inline std::vector<const LadderStep*> stepsOf(
        const std::vector<LadderStep>& ladder)
{
    std::vector<const LadderStep*> steps;
    steps.reserve(ladder.size());
    for (const LadderStep& s : ladder) {
        steps.push_back(&s);
    }
    return steps;
}

inline std::string stepTestName(
        const ::testing::TestParamInfo<const LadderStep*>& info)
{
    std::string name = info.param->name;
    std::replace_if(
            name.begin(),
            name.end(),
            [](unsigned char c) { return std::isalnum(c) == 0; },
            '_');
    return name;
}

// Checks that `compress` writes the graph of `s` on every frame that the
// test checks, with the same block counts.
inline void expectGraphOnEveryFrame(
        const LadderStep& s,
        const std::function<std::string(const std::string&)>& compress)
{
    const bool large = s.frameBytes >= kLargeFrameBytes;
    std::set<std::string> signatures;
    for (uint64_t seed = 1; seed <= (large ? 4 : 10); ++seed) {
        WorkloadSpec spec = s.spec;
        spec.seed         = seed;
        for (size_t frame = 0; frame < (large ? 2 : 10); ++frame) {
            const std::string compressed =
                    compress(generateFrame(spec, s.frameBytes, frame));
            EXPECT_EQ(decodeGraphShape(compressed), s.graph)
                    << s.name << " seed " << seed << " frame " << frame;
            signatures.insert(decodeGraphSignature(compressed));
        }
    }
    EXPECT_EQ(signatures.size(), 1U) << s.name;
}

} // namespace openzl::gpu::testkit
