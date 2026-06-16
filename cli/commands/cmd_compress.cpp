// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CParam.hpp"
#include "openzl/zl_compress.h"

#include "tools/io/InputSetStatic.h"
#include "tools/io/OutputBuffer.h"
#include "tools/logger/Logger.h"

#include "cli/args/TrainArgs.h"
#include "cli/commands/cmd_compress.h"
#include "cli/commands/cmd_train.h"
#include "cli/utils/util.h"

using namespace openzl::tools;
using namespace logger;

namespace openzl::cli {
constexpr size_t BYTES_TO_MB = 1000 * 1000;

namespace {

int validateCompressArgs(const CompressArgs& args)
{
    if (!args.output) {
        Logger::log(
                ERRORS,
                "No output file specified. Please provide a path using the -o or --output flag.");
        return 1;
    }

    return 0;
}

/**
 * Trains a compressor on the provided sample file.
 *
 * @note currently this method trains using the default trainer defined in
 *       tools/training/clustering/train_api.cpp.
 *
 * @return 0 on success, non-zero on failure.
 */
int trainCompressorOnSampleFile(CompressArgs& args)
{
    Logger::log(
            VERBOSE1,
            "Training compressor on sample file ",
            std::string(args.input->name()).c_str());

    // Convert single input to a vector of inputs for training
    std::vector<std::shared_ptr<tools::io::Input>> inputVec{ args.input };

    // Create an output buffer to store the trained compressor
    std::ostringstream compressorData;
    auto compressorOutput =
            std::make_shared<tools::io::OutputBuffer>(compressorData);

    // Construct args for training
    TrainArgs trainArgs(args, args.compressor());
    trainArgs.inputs =
            std::make_unique<tools::io::InputSetStatic>(std::move(inputVec));
    trainArgs.output = compressorOutput;
    if (args.trainInlineTestLimit) {
        trainArgs.trainParams.maxTimeSecs = args.trainInlineTestLimit.value();
    }

    // Train the compressor
    int result = cmdTrain(trainArgs);
    if (result != 0) {
        return result;
    }

    // Save the trained compressor
    args.setCompressor(
            custom_parsers::createCompressorFromSerialized(
                    compressorOutput->to_input()->contents(), ""));

    return result;
}

void writeTrace(CCtx& cctx, const CompressArgs& args)
{
    const auto trace = cctx.getLatestTrace();
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

int performCompression(const CompressArgs& args)
{
    // create compressor and context
    CCtx cctx;
    cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    if (!args.strict) {
        cctx.setParameter(CParam::PermissiveCompression, 1);
    }
    if (!args.storeOnExpansion) {
        cctx.setParameter(CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    }
    cctx.refCompressor(*args.compressor());
    if (args.traceOutput) {
        args.traceOutput->open();
        Logger::log(
                VERBOSE1,
                "Tracing compression to ",
                args.traceOutput->name().data());
        cctx.writeTraces(true, args.streamPreview);
    }

    auto& input  = *args.input;
    auto& output = *args.output;

    // ahead of time.
    const auto inputSize = input.size().value();
    Logger::log(VERBOSE1, "Input size: ", inputSize);

    // When StoreOnExpansion is disabled (train-inline or explicit flag),
    // compression may expand data beyond ZL_compressBound(), which assumes the
    // anti-inflation guard limits output to input + overhead. Use a generous
    // buffer in that case.
    size_t const dstCapacity = (args.trainInline || !args.storeOnExpansion)
            ? 2 * ZL_compressBound(inputSize) + 1024
            : ZL_compressBound(inputSize);
    std::string dstBuffer    = std::string(dstCapacity, '\0');

    // read the input
    const auto srcBuffer = input.contents();

    // compress
    const auto start = std::chrono::steady_clock::now();

    size_t compressedSize;
    try {
        compressedSize = cctx.compressSerial(dstBuffer, srcBuffer);
    } catch (const openzl::Exception&) {
        // if tracing, write the error trace to the output file
        if (args.traceOutput) {
            writeTrace(cctx, args);
        }
        throw;
    }

    util::logWarnings(cctx);

    const auto end     = std::chrono::steady_clock::now();
    const auto time_ms = std::chrono::duration<double, std::milli>(end - start);

    const auto time_s       = time_ms.count() / 1000.0;
    const auto inputSize_mb = (double)inputSize / BYTES_TO_MB;

    const auto compressionSpeed = inputSize_mb / time_s;

    // write output
    dstBuffer.resize(compressedSize);
    Logger::log_c(
            INFO,
            "Compressed %zu -> %zu (%.2fx) in %.3f ms, %.2f MB/s",
            srcBuffer.size(),
            dstBuffer.size(),
            (double)srcBuffer.size() / dstBuffer.size(),
            time_ms.count(),
            compressionSpeed);
    output.write(dstBuffer);
    output.close();

    // if tracing, write the trace to the output file
    if (args.traceOutput) {
        writeTrace(cctx, args);
    }
    return 0;
}

} // anonymous namespace

int cmdCompress(CompressArgs args)
{
    int validationResult = validateCompressArgs(args);
    if (validationResult != 0) {
        return validationResult;
    }

    if (args.trainInline) {
        int trainResult = trainCompressorOnSampleFile(args);
        if (trainResult != 0) {
            return trainResult;
        }
    }
    return performCompression(args);
}

} // namespace openzl::cli
