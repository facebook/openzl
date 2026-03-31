// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"

using namespace testing;

namespace openzl::tests {
namespace {

struct Foo {};

const Foo kFoo;

ZL_RESULT_DECLARE_TYPE(Foo);

class TestException : public testing::Test {
   public:
};
} // namespace

TEST_F(TestException, unwrapSuccess)
{
    try {
        unwrap(ZL_returnSuccess(), "Shouldn't throw!", nullptr);
    } catch (const Exception&) {
        EXPECT_TRUE(false) << "shouldn't throw!";
    } catch (...) {
        EXPECT_TRUE(false) << "shouldn't throw anything else!";
    }
}

TEST_F(TestException, unwrapFoo)
{
    try {
        unwrap(ZL_RESULT_WRAP_VALUE(Foo, kFoo), "Shouldn't throw!", nullptr);
    } catch (const Exception&) {
        EXPECT_TRUE(false) << "shouldn't throw!";
    } catch (...) {
        EXPECT_TRUE(false) << "shouldn't throw anything else!";
    }
}

TEST_F(TestException, unwrapErrorNullCtx)
{
    try {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        unwrap(ZL_RESULT_MAKE_ERROR(Foo, corruption, "Beep boop!"),
               "Should throw!",
               nullptr);
        EXPECT_TRUE(false) << "should be unreachable!";
    } catch (const Exception& ex) {
        const std::string what{ ex.what() };
        EXPECT_NE(what.find("Corruption detected"), std::string::npos) << what;
        EXPECT_NE(what.find("Should throw!"), std::string::npos) << what;
    } catch (...) {
        EXPECT_TRUE(false) << "shouldn't throw anything else!";
    }
}

TEST_F(TestException, unwrapErrorCppCtx)
{
    try {
        CCtx ctx;
        auto result = ZL_CCtx_compress(ctx.get(), NULL, 0, "1234567890", 10);
        unwrap(result, "Should throw!", &ctx);
        EXPECT_TRUE(false) << "should be unreachable!";
    } catch (const Exception& ex) {
        const std::string what{ ex.what() };
        // most stable proxy for having rich info in the error?
        EXPECT_NE(what.find("CCTX_compress"), std::string::npos) << what;
        EXPECT_NE(what.find("Should throw!"), std::string::npos) << what;
    } catch (...) {
        EXPECT_TRUE(false) << "shouldn't throw anything else!";
    }
}

TEST_F(TestException, unwrapErrorCCtx)
{
    try {
        CCtx ctx;
        auto result = ZL_CCtx_compress(ctx.get(), NULL, 0, "1234567890", 10);
        unwrap(result, "Should throw!", ctx.get());
        EXPECT_TRUE(false) << "should be unreachable!";
    } catch (const Exception& ex) {
        const std::string what{ ex.what() };
        // most stable proxy for having rich info in the error?
        EXPECT_NE(what.find("CCTX_compress"), std::string::npos) << what;
        EXPECT_NE(what.find("Should throw!"), std::string::npos) << what;
    } catch (...) {
        EXPECT_TRUE(false) << "shouldn't throw anything else!";
    }
}

TEST_F(TestException, unwrapWithAllCtxTypes)
{
    CCtx const* const cctx                                 = nullptr;
    DCtx const* const dctx                                 = nullptr;
    Compressor const* const compressor                     = nullptr;
    ZL_CCtx const* const zl_cctx                           = nullptr;
    ZL_DCtx const* const zl_dctx                           = nullptr;
    ZL_Compressor const* const zl_compressor               = nullptr;
    ZL_CompressorSerializer const* const zl_serializer     = nullptr;
    ZL_CompressorDeserializer const* const zl_deserializer = nullptr;

    const auto result = ZL_RESULT_WRAP_VALUE(Foo, kFoo);

    unwrap(result, "", cctx);
    unwrap(result, "", dctx);
    unwrap(result, "", compressor);
    unwrap(result, "", zl_cctx);
    unwrap(result, "", zl_dctx);
    unwrap(result, "", zl_compressor);
    unwrap(result, "", zl_serializer);
    unwrap(result, "", zl_deserializer);
}
} // namespace openzl::tests
