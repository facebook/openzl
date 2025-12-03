// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/openzl.hpp"

using namespace testing;

namespace openzl::tests {

TEST(TestLocalParams, get)
{
    LocalParams params;
    ASSERT_NE(params.get(), nullptr);
}

TEST(TestLocalParams, addIntParam)
{
    LocalParams params;
    params.addIntParam(0, 1);
    params.addIntParam(2, 2);
    params.addIntParam({ 1, 1 });

    ASSERT_EQ(params.getIntParams().size(), 3);
    ASSERT_EQ(params.getIntParams()[0].paramId, 0);
    ASSERT_EQ(params.getIntParams()[0].paramValue, 1);
    ASSERT_EQ(params.getIntParams()[1].paramId, 2);
    ASSERT_EQ(params.getIntParams()[2].paramId, 1);

    ASSERT_THROW(params.addIntParam(0, 0), Exception);
}

TEST(TestLocalParams, addCopyParam)
{
    LocalParams params;
    {
        int x = 42;
        params.addCopyParam({ 42, &x, sizeof(x) });
    }
    {
        int64_t y = 350;
        params.addCopyParam(350, &y, sizeof(y));
    }
    struct Foo {
        int x;
        int y;
    };
    params.addCopyParam(0, Foo{ 42, 350 });

    auto p = params.getCopyParams();

    ASSERT_EQ(p.size(), 3);

    ASSERT_EQ(p[0].paramId, 42);
    ASSERT_EQ(p[0].paramSize, 4);
    ASSERT_EQ(*(const int*)p[0].paramPtr, 42);

    ASSERT_EQ(p[1].paramId, 350);
    ASSERT_EQ(p[1].paramSize, 8);
    ASSERT_EQ(*(const int64_t*)p[1].paramPtr, 350);

    ASSERT_EQ(p[2].paramId, 0);
    ASSERT_EQ(p[2].paramSize, 8);
    ASSERT_EQ(((const Foo*)p[2].paramPtr)->x, 42);
    ASSERT_EQ(((const Foo*)p[2].paramPtr)->y, 350);

    ASSERT_THROW(params.addCopyParam(42, 0), Exception);
}

TEST(TestLocalParams, addRefParam)
{
    LocalParams params;
    int x = 42;
    int y = 350;
    params.addRefParam({ 0, &x });
    params.addRefParam(1, &y);

    auto p = params.getRefParams();
    ASSERT_EQ(p.size(), 2);

    ASSERT_EQ(p[0].paramId, 0);
    ASSERT_EQ(p[0].paramRef, &x);
    ASSERT_EQ(p[1].paramRef, &y);

    ASSERT_THROW(params.addCopyParam({ 0, &x, sizeof(x) }), Exception);
}

TEST(TestLocalParams, addRefParamWithSize)
{
    LocalParams params;
    int x     = 42;
    int64_t y = 350;
    params.addRefParam(0, &x, sizeof(x));
    params.addRefParam(1, &y, sizeof(y));

    auto p = params.getRefParams();
    ASSERT_EQ(p.size(), 2);

    ASSERT_EQ(p[0].paramId, 0);
    ASSERT_EQ(p[0].paramRef, &x);
    ASSERT_EQ(p[0].paramSize, sizeof(x));

    ASSERT_EQ(p[1].paramId, 1);
    ASSERT_EQ(p[1].paramRef, &y);
    ASSERT_EQ(p[1].paramSize, sizeof(y));
}

TEST(TestLocalParams, move)
{
    auto params = std::make_unique<LocalParams>();
    int x       = 350;
    params->addIntParam(0, x);
    params->addCopyParam(1, x);
    params->addRefParam(2, &x);

    auto params2 = std::move(*params);
    params.reset();

    ASSERT_EQ(params2.getIntParams().size(), 1);
    ASSERT_EQ(params2.getIntParams()[0].paramValue, 350);
    ASSERT_EQ(params2.getCopyParams().size(), 1);
    ASSERT_EQ(*(const int*)params2.getCopyParams()[0].paramPtr, 350);
    ASSERT_EQ(params2.getRefParams().size(), 1);
    ASSERT_EQ((const int*)params2.getRefParams()[0].paramRef, &x);

    ASSERT_EQ(params2->intParams.intParams, params2.getIntParams().data());
    ASSERT_EQ(params2->intParams.nbIntParams, params2.getIntParams().size());

    ASSERT_EQ(params2->copyParams.copyParams, params2.getCopyParams().data());
    ASSERT_EQ(params2->copyParams.nbCopyParams, params2.getCopyParams().size());

    ASSERT_EQ(params2->refParams.refParams, params2.getRefParams().data());
    ASSERT_EQ(params2->refParams.nbRefParams, params2.getRefParams().size());
}

TEST(TestLocalParams, duplicateKeysAcrossParams)
{
    LocalParams params;
    params.addIntParam(0, 0);
    params.addCopyParam(1, 1);
    params.addRefParam(2, nullptr);

    ASSERT_THROW(params.addIntParam(0, 0), Exception);
    ASSERT_THROW(params.addCopyParam(0, 0), Exception);
    ASSERT_THROW(params.addRefParam(0, nullptr), Exception);

    ASSERT_THROW(params.addIntParam(1, 0), Exception);
    ASSERT_THROW(params.addCopyParam(1, 0), Exception);
    ASSERT_THROW(params.addRefParam(1, nullptr), Exception);

    ASSERT_THROW(params.addIntParam(2, 0), Exception);
    ASSERT_THROW(params.addCopyParam(2, 0), Exception);
    ASSERT_THROW(params.addRefParam(2, nullptr), Exception);
}

TEST(TestLocalParams, copyConstruct)
{
    auto params = std::make_unique<LocalParams>();
    int x       = 350;
    params->addIntParam(0, x);
    params->addCopyParam(1, x);
    params->addRefParam(2, &x);

    LocalParams params2(*params);
    params.reset();

    ASSERT_EQ(params2.getIntParams().size(), 1);
    ASSERT_EQ(params2.getIntParams()[0].paramValue, 350);
    ASSERT_EQ(params2.getCopyParams().size(), 1);
    ASSERT_EQ(*(const int*)params2.getCopyParams()[0].paramPtr, 350);
    ASSERT_EQ(params2.getRefParams().size(), 1);
    ASSERT_EQ((const int*)params2.getRefParams()[0].paramRef, &x);

    ASSERT_EQ(params2->intParams.intParams, params2.getIntParams().data());
    ASSERT_EQ(params2->intParams.nbIntParams, params2.getIntParams().size());

    ASSERT_EQ(params2->copyParams.copyParams, params2.getCopyParams().data());
    ASSERT_EQ(params2->copyParams.nbCopyParams, params2.getCopyParams().size());

    ASSERT_EQ(params2->refParams.refParams, params2.getRefParams().data());
    ASSERT_EQ(params2->refParams.nbRefParams, params2.getRefParams().size());
}

TEST(TestLocalParams, copyAssign)
{
    auto params = std::make_unique<LocalParams>();
    int x       = 350;
    params->addIntParam(0, x);
    params->addCopyParam(1, x);
    params->addRefParam(2, &x);

    LocalParams params2;
    params2.addIntParam(10, x);
    params2.addCopyParam(11, x);
    params2.addRefParam(12, &x);
    params2 = *params;

    params.reset();

    ASSERT_EQ(params2.getIntParams().size(), 1);
    ASSERT_EQ(params2.getIntParams()[0].paramValue, 350);
    ASSERT_EQ(params2.getCopyParams().size(), 1);
    ASSERT_EQ(*(const int*)params2.getCopyParams()[0].paramPtr, 350);
    ASSERT_EQ(params2.getRefParams().size(), 1);
    ASSERT_EQ((const int*)params2.getRefParams()[0].paramRef, &x);

    ASSERT_EQ(params2->intParams.intParams, params2.getIntParams().data());
    ASSERT_EQ(params2->intParams.nbIntParams, params2.getIntParams().size());

    ASSERT_EQ(params2->copyParams.copyParams, params2.getCopyParams().data());
    ASSERT_EQ(params2->copyParams.nbCopyParams, params2.getCopyParams().size());

    ASSERT_EQ(params2->refParams.refParams, params2.getRefParams().data());
    ASSERT_EQ(params2->refParams.nbRefParams, params2.getRefParams().size());
}
} // namespace openzl::tests
