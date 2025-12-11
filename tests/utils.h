// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>

#include <gtest/gtest.h>

#include "openzl/cpp/Compressor.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_errors.h"

#include "openzl/common/cursor.h"
#include "openzl/common/debug.h"

////////////////////////////////////////
// Macros to Adapt Zstrong Success or Failure to GTest Success or Failure
////////////////////////////////////////

#define ASSERT_ZS_VALID(x) ASSERT_FALSE(ZL_RES_isError((x)))
#define EXPECT_ZS_VALID(x) EXPECT_FALSE(ZL_RES_isError((x)))
#define ASSERT_ZS_ERROR(x) ASSERT_TRUE(ZL_RES_isError((x)))
#define EXPECT_ZS_ERROR(x) EXPECT_TRUE(ZL_RES_isError((x)))

#if ZL_ENABLE_ASSERT
#    define ZS_CHECK_ASSERT_FIRES(expr) ASSERT_DEATH({ expr; }, "")
#else
#    define ZS_CHECK_ASSERT_FIRES(expr) expr
#endif

#if ZL_ENABLE_REQUIRE
#    define ZS_CHECK_REQUIRE_FIRES(expr) ASSERT_DEATH({ expr; }, "")
#else
#    define ZS_CHECK_REQUIRE_FIRES(expr) expr
#endif

namespace openzl {
namespace tests {

extern const std::string kEmptyTestInput;
extern const std::string kFooTestInput;
extern const std::string kLoremTestInput;
extern const std::string kAudioPCMS32LETestInput;
extern const std::string kUniqueCharsTestInput;
extern const std::string kMoviesCsvFormatInput;
extern const std::string kStudentGradesCsvFormatInput;
extern const uint32_t kSampleTrainedCsvColumnMapping[];

/**
 * RAII wrapper for Write Cursors backed by a std::string. The main advantage is
 * that at destruction time, the written size is reflected back into the string.
 */
class ZS_WC_StrWrapper {
   public:
    explicit ZS_WC_StrWrapper(std::string& str) : str_(str), wc_()
    {
        size_t pos = str_.size();
        str_.resize(str_.capacity());
        wc_ = ZL_WC_wrapPartial((uint8_t*)&str_[0], pos, str_.size());
    }

    ~ZS_WC_StrWrapper()
    {
        str_.resize(ZL_WC_size(&wc_));
    }

    ZL_WC* operator*()
    {
        return &wc_;
    }

   private:
    std::string& str_;
    ZL_WC wc_;
};

ZL_INLINE ZL_RC ZS_RC_wrapStr(const std::string& str)
{
    return ZL_RC_wrap((const uint8_t*)str.data(), str.size());
}

ZL_INLINE std::string ZS_RC_toStr(const ZL_RC* rc)
{
    return std::string((const char*)ZL_RC_ptr(rc), ZL_RC_avail(rc));
}

/**
 * @returns a graph that converts @p inStreamType to the input expected by graph
 * @p graph, and then forwards to @p graph. If the @p inStreamType is
 * ZL_Type_serial and the @p graph type is not, then you must provide @p
 * eltWidth.
 */
ZL_GraphID addConversionToGraph(
        ZL_Compressor* cgraph,
        ZL_GraphID graph,
        ZL_Type inStreamType,
        int eltWidth = 0);

/**
 * Builds a trivial graph using the node. It accepts the same input type as @p
 * node.
 */
ZL_GraphID buildTrivialGraph(ZL_Compressor* cgraph, ZL_NodeID node);

} // namespace tests
} // namespace openzl
