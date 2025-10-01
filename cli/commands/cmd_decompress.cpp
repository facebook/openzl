// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/commands/cmd_decompress.h"
#include "cli/utils/util.h"

#include <chrono>

#include "tools/logger/Logger.h"

#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/zl_decompress.h"

namespace openzl::cli {
constexpr size_t BYTES_TO_MB = 1000 * 1000;

using namespace tools::logger;

int cmdDecompress(const DecompressArgs& args)
{
    if (!args.output) {
        Logger::log(
                ERRORS,
                "No output file specified. Please provide a path using the -o or --output flag.");
        return 1;
    }

    auto& input  = *args.input;
    auto& output = *args.output;

    // TODO: eventually, handle streamed inputs that don't know their size
    // ahead of time.
    const auto inputSize = input.size().value();

    Logger::log(VERBOSE1, "Input size: ", inputSize);

    // read the input
    const auto srcBuffer = input.contents();

    const auto start = std::chrono::steady_clock::now();

    DCtx dctx;

    // decompress
    std::string dstBuffer = dctx.decompressSerial(srcBuffer);

    util::logWarnings(dctx);

    const auto end     = std::chrono::steady_clock::now();
    const auto time_ms = std::chrono::duration<double, std::milli>(end - start);

    const auto time_s              = time_ms.count() / 1000.0;
    const auto decompressedSize_mb = (double)dstBuffer.size() / BYTES_TO_MB;

    const auto compressionSpeed = decompressedSize_mb / time_s;

    Logger::log_c(
            INFO,
            "Decompressed: %2.2f%% (%s -> %s) in %.3f ms, %.2f MB/s",
            (double)srcBuffer.size() / dstBuffer.size() * 100,
            util::sizeString(srcBuffer.size()).c_str(),
            util::sizeString(dstBuffer.size()).c_str(),
            time_ms.count(),
            compressionSpeed);
    output.write(std::move(dstBuffer));
    output.close();
    return 0;
}

} // namespace openzl::cli
