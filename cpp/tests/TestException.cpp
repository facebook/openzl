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

class ExceptionFunctionGraph : public FunctionGraph {
   public:
    FunctionGraphDescription functionGraphDescription() const override
    {
        return {
            .name           = "unwrap_function_graph",
            .inputTypeMasks = { TypeMask::Serial },
        };
    }

    void graph(GraphState& state) const override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(state.get());

        const char* bar = "bar";
        auto result     = ZL_REPORT_ERROR(GENERIC, "foo%s", bar);
        state.unwrap(result);
    }
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

TEST_F(TestException, unwrapForGraphState)
{
    Compressor compressor;
    compressor.selectStartingGraph(compressor.registerFunctionGraph(
            std::make_shared<ExceptionFunctionGraph>()));
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    try {
        CCtx cctx;
        cctx.refCompressor(compressor);
        cctx.compressSerial("hello world hello hello hello hello");
        EXPECT_TRUE(false) << "should be unreachable";
    } catch (const Exception& ex) {
        const std::string what{ ex.what() };
        EXPECT_NE(what.find("foobar"), std::string::npos) << what;
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
    ZL_Graph const* const zl_graph                         = nullptr;

    const auto result = ZL_RESULT_WRAP_VALUE(Foo, kFoo);

    unwrap(result, "", cctx);
    unwrap(result, "", dctx);
    unwrap(result, "", compressor);
    unwrap(result, "", zl_cctx);
    unwrap(result, "", zl_dctx);
    unwrap(result, "", zl_compressor);
    unwrap(result, "", zl_serializer);
    unwrap(result, "", zl_deserializer);
    unwrap(result, "", zl_graph);
}

TEST_F(TestException, backToCErrorWithoutContext)
{
    CCtx ctx;
    auto result = ZL_CCtx_compress(ctx.get(), NULL, 0, "1234567890", 10);
    ASSERT_TRUE(ZL_RES_isError(result));
    const auto orig_code = ZL_E_code(ZL_RES_error(result));
    const std::string orig_str{ ZL_CCtx_getErrorContextString_fromError(
            ctx.get(), ZL_RES_error(result)) };
    try {
        unwrap(result, "Whoopsie!", ctx.get());
        EXPECT_TRUE(false) << "should be unreachable!";
    } catch (const Exception& ex) {
        const auto e    = ex.toError();
        const auto code = ZL_E_code(e);
        EXPECT_NE(code, ZL_ErrorCode_no_error);
        EXPECT_NE(code, ZL_ErrorCode_GENERIC);
        EXPECT_EQ(code, orig_code);
        const std::string str{ ZL_CCtx_getErrorContextString_fromError(
                ctx.get(), e) };
        // No error context string should be present
        EXPECT_EQ(str.find(orig_str), std::string::npos) << str;
    } catch (...) {
        EXPECT_TRUE(false) << "shouldn't throw anything else!";
    }
}

TEST_F(TestException, backToCErrorWithContext)
{
    CCtx ctx;
    auto result = ZL_CCtx_compress(ctx.get(), NULL, 0, "1234567890", 10);
    ASSERT_TRUE(ZL_RES_isError(result));
    const auto orig_code = ZL_E_code(ZL_RES_error(result));
    const std::string orig_str{ ZL_CCtx_getErrorContextString_fromError(
            ctx.get(), ZL_RES_error(result)) };
    try {
        unwrap(result, "Whoopsie!", ctx.get());
        EXPECT_TRUE(false) << "should be unreachable!";
    } catch (const Exception& ex) {
        const auto e    = ex.toError(ctx);
        const auto code = ZL_E_code(e);
        EXPECT_NE(code, ZL_ErrorCode_no_error);
        EXPECT_NE(code, ZL_ErrorCode_GENERIC);
        EXPECT_EQ(code, orig_code);
        const std::string str{ ZL_CCtx_getErrorContextString_fromError(
                ctx.get(), e) };
        EXPECT_NE(str.find("Whoopsie!"), std::string::npos) << str;
        EXPECT_NE(str.find(orig_str), std::string::npos) << str;
    } catch (...) {
        EXPECT_TRUE(false) << "shouldn't throw anything else!";
    }
}
} // namespace openzl::tests
