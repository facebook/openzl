// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/LocalParamsProducer.h"

#include "tests/local_params_utils.h"

using namespace ::testing;

namespace openzl {
namespace tests {

namespace {
class LocalParamsTest : public Test {
   protected:
    void SetUp() override
    {
        auto gen = std::make_shared<std::mt19937>(0xdeadbeef);
        auto rw  = std::make_shared<datagen::PRNGWrapper>(gen);
        lpp_     = std::make_unique<datagen::LocalParamsProducer>(rw);
    }

    datagen::LocalParamsProducer& lpp()
    {
        return *lpp_;
    }

    std::unique_ptr<datagen::LocalParamsProducer> lpp_;
};
} // anonymous namespace

TEST_F(LocalParamsTest, ComparisonOfRandomParamsWithThemselves)
{
    for (size_t i = 0; i < 10000; i++) {
        const auto lp = lpp()("localparams");
        LocalParams_check_eq(lp, lp);
    }
}

TEST_F(LocalParamsTest, ComparisonOfRandomParamsPreservingEquality)
{
    for (size_t i = 0; i < 10000; i++) {
        const auto lp1 = lpp()("localparams");
        const auto lp2 = lpp().mutateParamsPreservingEquality(lp1);
        LocalParams_check_eq(lp1, lp2);
        LocalParams_check_eq(lp2, lp1);
    }
}

TEST_F(LocalParamsTest, ComparisonOfRandomParamsPerturbingEquality)
{
    for (size_t i = 0; i < 10000; i++) {
        const auto lp1 = lpp()("localparams");
        const auto lp2 = lpp().mutateParamsPreservingEquality(lp1);
        const auto lp3 = lpp().mutateParamsPerturbingEquality(lp2);
        LocalParams_check_eq(lp1, lp2);
        LocalParams_check_eq(lp2, lp1);
        LocalParams_check_ne(lp1, lp3);
        LocalParams_check_ne(lp3, lp1);
        LocalParams_check_ne(lp2, lp3);
        LocalParams_check_ne(lp3, lp2);
    }
}

TEST_F(LocalParamsTest, ComparisonOfRandomParams)
{
    for (size_t i = 0; i < 10000; i++) {
        const auto lp1 = lpp()("localparams");
        const auto lp2 = lpp()("localparams2");
        LocalParams_check_ne(lp1, lp2);
        LocalParams_check_ne(lp2, lp1);
    }
}

} // namespace tests
} // namespace openzl
