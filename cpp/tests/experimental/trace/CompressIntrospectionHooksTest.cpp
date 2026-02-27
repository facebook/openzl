// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <fstream>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Conversion.hpp"
#include "openzl/cpp/codecs/FieldLz.hpp"

using namespace ::testing;

namespace openzl {

TEST(CompressIntrospectionHooksTest, writeTestFile)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    std::vector<uint32_t> vals;
    vals.reserve(1000 * 11);
    for (size_t i = 0; i < 1000; ++i) {
        for (uint32_t j = 1; j < 12; ++j) {
            vals.push_back(j);
        }
    }

    auto starting = nodes::ConvertSerialToNumLE32{}(
            compressor, graphs::FieldLz{ 2 }(compressor));
    compressor.selectStartingGraph(starting);

    auto input = Input::refSerial(vals.data(), vals.size() * sizeof(vals[0]));
    CCtx cctx;
    cctx.refCompressor(compressor);
    cctx.writeTraces(true);
    auto str         = cctx.compressOne(input);
    const auto trace = cctx.getLatestTrace();
    if ((0)) {
        std::ofstream out("/tmp/streamdump.cbor", std::ios::binary);
        ASSERT_TRUE(out);
        out << trace.first;
        out.close();
    }
}

} // namespace openzl
