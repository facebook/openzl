// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <stdint.h>
#include <vector>
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "tests/datagen/DataGen.h"

#pragma once

namespace openzl::thrift::tests {

class ProbSelectorTest : public ::testing::Test {
   public:
    ::openzl::tests::datagen::DataGen dataGen_;

    void testRoundTrip(
            ZL_GraphID* selGraphs,
            size_t* probWeights,
            size_t nbSuccessors,
            std::vector<uint8_t>& compressSample);

    size_t compressWithSelector(
            ZL_GraphID* selGraphs,
            size_t* probWeights,
            size_t nbSuccessors,
            std::vector<uint8_t>& compressSample);

    size_t compressWithGid(
            ZL_GraphID gid,
            std::vector<uint8_t>& compressSample);

   private:
    ZL_Report compress(
            ZL_Compressor* const cgraph,
            void* dstBuff,
            size_t dstCapacity,
            const void* src,
            size_t srcSize,
            ZL_GraphID graphid);
    ZL_Report decompress(
            void* dst,
            size_t dstCapacity,
            const void* compressed,
            size_t cSize);
};
} // namespace openzl::thrift::tests
