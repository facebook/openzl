// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/commands/cmd_decompress.h"
#include "cli/utils/util.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "tools/logger/Logger.h"

#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Exception.hpp"

namespace openzl::cli {
constexpr size_t BYTES_TO_MB = 1000 * 1000;

using namespace tools::logger;

namespace {

void writeTrace(DCtx& dctx, const DecompressArgs& args)
{
    const auto trace = dctx.getLatestTrace();
    args.traceOutput->write(trace.first);
    args.traceOutput->close();
    if (args.traceStreamsDir) {
        std::filesystem::path dir{ *args.traceStreamsDir };
        if (!std::filesystem::is_directory(dir)) {
            std::string msg = "Streamdump trace directory does not exist: "
                    + dir.string();
            throw InvalidArgsException(msg);
        }
        for (const auto& [id, stream] : trace.second) {
            std::filesystem::path path = dir / (id + ".sdd");
            std::ofstream file{ path, std::ios::binary };
            if (!file.is_open()) {
                Logger::log(
                        ERRORS,
                        "Failed to open streamdump file: ",
                        path.string().c_str());
                continue;
            }
            file.write(stream.first.data(), stream.first.size());
            if (stream.second != "") {
                std::filesystem::path strLensPath = dir / (id + ".sdlens");
                std::ofstream strLensFile{ strLensPath, std::ios::binary };
                if (!strLensFile.is_open()) {
                    Logger::log(
                            ERRORS,
                            "Failed to open streamdump strlens file: ",
                            strLensPath.string().c_str());
                    continue;
                }
                strLensFile.write(stream.second.data(), stream.second.size());
            }
            file.close();
        }
    }
}

} // anonymous namespace

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

    if (args.traceOutput) {
        args.traceOutput->open();
        Logger::log(
                VERBOSE1,
                "Tracing decompression to ",
                args.traceOutput->name().data());
        dctx.writeTraces(true, args.streamPreview);
    }

    // decompress
    std::string dstBuffer;
    try {
        dstBuffer = dctx.decompressSerial(srcBuffer);
    } catch (const openzl::Exception&) {
        // if tracing, write the error trace to the output file
        if (args.traceOutput) {
            writeTrace(dctx, args);
        }
        throw;
    }

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

    // if tracing, write the trace to the output file
    if (args.traceOutput) {
        writeTrace(dctx, args);
    }
    return 0;
}

} // namespace openzl::cli
