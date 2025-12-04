// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdio.h>
#include <string>

#include <openzl/openzl.h>
#include <openzl/openzl.hpp>

// Smoke test to ensure that everything builds & runs

int main(int argc, const char** argv)
{
    openzl::Compressor compressor;
    compressor.unwrap(ZL_Compressor_selectStartingGraphID(
            compressor.get(), ZL_GRAPH_ZSTD));
    compressor.setParameter(
            openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    std::string data(10000, 'a');
    openzl::CCtx cctx;
    cctx.refCompressor(compressor);
    auto compressed = cctx.compressSerial(std::string(10000, 'a'));

    std::string decompressed(10000, 'b');
    auto report = ZL_decompress(
            decompressed.data(),
            decompressed.size(),
            compressed.data(),
            compressed.size());
    auto size = openzl::unwrap(report);
    if (size != decompressed.size()) {
        fprintf(stderr, "decompressed size wrong\n");
        return 1;
    }
    if (decompressed != data) {
        fprintf(stderr, "corruption\n");
        return 2;
    }
    fprintf(stderr, "success\n");
    return 0;
}
