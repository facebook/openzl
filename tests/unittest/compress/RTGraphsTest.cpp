// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>

#include "openzl/compress/rtgraphs.h"

using namespace ::testing;

namespace openzl::tests {

TEST(RTGraphsTest, WHENcreateAndDeleteStreamsTHENallStreamsHaveUniqueID)
{
    RTGraph* rtgm = (RTGraph*)calloc(1, sizeof(*rtgm));
    EXPECT_FALSE(ZL_isError(RTGM_init(rtgm)));

    CNode bruteForceCnode = { .nodetype      = node_internalTransform,
                              .transformDesc = {
                                      .publicDesc = { .gd = {
                                                              .nbSOs = 30,
                                                      } } } };

    int times           = 3;
    const void* garbage = &times;
    while (times--) {
        auto rtn = RTGM_createNode(
                rtgm, &bruteForceCnode, (const RTStreamID*)garbage, 0);
        EXPECT_FALSE(ZL_RES_isError(rtn));
        auto rtnid                     = ZL_RES_value(rtn);
        std::vector<bool> createOrFree = { 1, 1, 1, 0, 1, 0, 0, 0,
                                           1, 1, 0, 0, 1, 0, 1, 0 };
        std::vector<RTStreamID> rtsids;
        std::vector<ZL_IDType> ids;
        size_t outputId = 0;
        for (const auto sign : createOrFree) {
            if (sign == 1) {
                auto rtsid = ZL_RES_value(RTGM_addStream(
                        rtgm, rtnid, outputId++, 0, ZL_Type_serial, 1, 1000));
                rtsids.push_back(rtsid);
                auto* const cstream = &VECTOR_AT(rtgm->streams, rtsid.rtsid);
                ids.push_back(ZL_Data_id(cstream->stream).sid);
            } else {
                RTGM_clearRTStream(rtgm, rtsids[0], 1);
                rtsids.erase(rtsids.begin(), rtsids.begin() + 1);
            }
        }
        std::sort(ids.begin(), ids.end());
        for (size_t i = 1; i < ids.size(); ++i) {
            EXPECT_NE(ids[i], ids[i - 1]);
        }
        RTGM_reset(rtgm);
    }
    RTGM_destroy(rtgm);
    free(rtgm);
}

} // namespace openzl::tests
