// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "custom_transforms/thrift/tests/test_prob_selector_fixture.h"
#include "openzl/zl_public_nodes.h"

namespace openzl::thrift::tests {

// A test that tests a few random inputs and checks the graph ID obtained is
// correct.
TEST_F(ProbSelectorTest, ProbSelectorOutputMatchesProbability)
{
    // We expect a binomial distribution in terms of the results of multipe
    // trials. This allows us to ensure with a good probability the test passes
    // regardless of the implementation of uniform distribution. Using the
    // regularized incomplete beta function we list the probabilities of failure
    // for each expect.

    int numZstd  = 0;
    int numStore = 0;
    int numHuff  = 0;
    int numOther = 0;
    for (size_t trial = 0; trial < 10000; trial++) {
        std::vector<uint8_t> sample1 =
                dataGen_.randVector<uint8_t>("sample1", 30, 128, 2000);

        auto zstd  = compressWithGid(ZL_GRAPH_ZSTD, sample1);
        auto huff  = compressWithGid(ZL_GRAPH_HUFFMAN, sample1);
        auto store = compressWithGid(ZL_GRAPH_STORE, sample1);
        // It is possible compression is indistinguishable, in this case retry
        if (zstd == huff || huff == store || zstd == store) {
            trial--;
            continue;
        }
        auto sel = compressWithSelector(
                (ZL_GraphID[]){
                        ZL_GRAPH_ZSTD, ZL_GRAPH_STORE, ZL_GRAPH_HUFFMAN },
                (size_t[]){ 1, 1, 1 },
                3,
                sample1);
        // Find the selector successor chosen
        if (sel == zstd) {
            numZstd++;
        } else if (sel == huff) {
            numHuff++;
        } else if (sel == store) {
            numStore++;
        } else {
            numOther++;
        }
    }
    // I_{2/3}(7000, 3001)= 5.3059-13
    EXPECT_GT(numZstd, 3000);
    EXPECT_GT(numStore, 3000);
    EXPECT_GT(numHuff, 3000);
    // Should never fail
    EXPECT_EQ(numOther, 0);
    EXPECT_EQ(numZstd + numStore + numHuff, 10000);

    // Reset stats
    numZstd  = 0;
    numStore = 0;
    numHuff  = 0;
    for (size_t trial = 0; trial < 10000; trial++) {
        std::vector<uint8_t> sample1 =
                dataGen_.randVector<uint8_t>("sample1", 30, 128, 2000);
        auto zstd  = compressWithGid(ZL_GRAPH_ZSTD, sample1);
        auto huff  = compressWithGid(ZL_GRAPH_HUFFMAN, sample1);
        auto store = compressWithGid(ZL_GRAPH_STORE, sample1);
        // It is possible compression is indistinguishable, in this case retry
        if (zstd == huff || huff == store || zstd == store) {
            trial--;
            continue;
        }
        auto sel = compressWithSelector(
                (ZL_GraphID[]){
                        ZL_GRAPH_ZSTD, ZL_GRAPH_STORE, ZL_GRAPH_HUFFMAN },
                (size_t[]){ 1, 4, 5 },
                3,
                sample1);
        // Find the selector successor chosen
        if (sel == zstd) {
            numZstd++;
        } else if (sel == huff) {
            numHuff++;
        } else if (sel == store) {
            numStore++;
        } else {
            numOther++;
        }
    }
    // I_{9/10}(9200, 801) = 3.4185e-12
    EXPECT_GT(numZstd, 800);
    // I_{6/10}(6400, 3601) = 1.1570e-16
    EXPECT_GT(numStore, 3600);
    // I_{5/10}(5400, 4601) = 6.5212-16
    EXPECT_GT(numHuff, 4600);
    // Should never fail
    EXPECT_EQ(numOther, 0);
    EXPECT_EQ(numZstd + numStore + numHuff, 10000);
}

TEST_F(ProbSelectorTest, ProbSelectorRoundTrip)
{
    for (size_t trial = 0; trial < 10; trial++) {
        std::vector<uint8_t> sample =
                dataGen_.template randVector<uint8_t>("sample", 30, 128, 1000);
        testRoundTrip(
                (ZL_GraphID[]){
                        ZL_GRAPH_ZSTD, ZL_GRAPH_STORE, ZL_GRAPH_HUFFMAN },
                (size_t[]){ 1, 1, 1 },
                3,
                sample);
    }
}
} // namespace openzl::thrift::tests
