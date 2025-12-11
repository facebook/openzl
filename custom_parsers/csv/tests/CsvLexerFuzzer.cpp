// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "security/lionhead/utils/lib_ftest/fdp/fdp/fdp_impl.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_parsers/csv/csv_profile.h"
#include "openzl/openzl.hpp"
#include "tests/datagen/random_producer/LionheadFDPWrapper.h"

using namespace ::testing;
using namespace facebook::security::lionhead::fdp;
using openzl::tests::datagen::LionheadFDPWrapper;

namespace openzl::custom_parsers {

class CsvLexerTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        auto gid = ZL_createGraph_genericCSVCompressor(compressor_.get());
        compressor_.selectStartingGraph(gid);
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.refCompressor(compressor_);
    }

    void roundtrip(std::string_view input)
    {
        std::string compressed;
        try {
            compressed = cctx_.compressSerial(input);
        } catch (openzl::Exception&) {
            return;
        }
        auto regen = dctx_.decompressSerial(compressed);
        ASSERT_EQ(regen, input);
    }

    CCtx cctx_{};
    DCtx dctx_{};
    Compressor compressor_{};
};

template <class HarnessMode>
LionheadFDPWrapper<StructuredFDP<HarnessMode>> rwFromFDP(
        StructuredFDP<HarnessMode>& fdp)
{
    return LionheadFDPWrapper<StructuredFDP<HarnessMode>>(fdp);
}

FUZZ_F(CsvLexerTest, RandomInputFuzzer)
{
    auto rw    = rwFromFDP(f);
    auto input = rw.all_remaining_bytes();
    roundtrip(std::string_view((const char*)input.data(), input.size()));
}

} // namespace openzl::custom_parsers
