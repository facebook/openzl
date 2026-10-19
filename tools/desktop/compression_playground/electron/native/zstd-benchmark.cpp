// Copyright (c) Meta Platforms, Inc. and affiliates.

// Benchmarks zstd the way `zli benchmark` benchmarks OpenZL, so the two can be
// compared: one reused context per direction, one-shot calls, steady_clock
// around the whole loop, and the leading columns of zli's --output-csv.
//
// Usage: zstd-benchmark <input> --level <n> --num-iters <n> --output-csv <path>

#include <zstd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string input;
    int level       = 3;
    size_t numIters = 1;
    std::string outputCsv;
};

Args parseArgs(int argc, char** argv)
{
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0) {
            args.input = arg;
            continue;
        }
        if (i + 1 >= argc) {
            throw std::invalid_argument("Missing value for " + arg);
        }
        const std::string value = argv[++i];
        if (arg == "--level") {
            args.level = std::stoi(value);
        } else if (arg == "--num-iters") {
            args.numIters = std::stoul(value);
        } else if (arg == "--output-csv") {
            args.outputCsv = value;
        } else {
            throw std::invalid_argument("Unknown option " + arg);
        }
    }
    if (args.input.empty() || args.outputCsv.empty() || args.numIters == 0) {
        throw std::invalid_argument(
                "Usage: zstd-benchmark <input> --level <n> --num-iters <n> --output-csv <path>");
    }
    return args;
}

std::vector<char> readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open input file: " + path);
    }
    return { std::istreambuf_iterator<char>(in), {} };
}

size_t check(size_t code, const char* operation)
{
    if (ZSTD_isError(code)) {
        throw std::runtime_error(
                std::string(operation) + " failed: " + ZSTD_getErrorName(code));
    }
    return code;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Args args             = parseArgs(argc, argv);
        const std::vector<char> src = readFile(args.input);

        std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> cctx(
                ZSTD_createCCtx(), ZSTD_freeCCtx);
        std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> dctx(
                ZSTD_createDCtx(), ZSTD_freeDCtx);
        if (!cctx || !dctx) {
            throw std::runtime_error("Cannot allocate zstd contexts");
        }
        check(ZSTD_CCtx_setParameter(
                      cctx.get(), ZSTD_c_compressionLevel, args.level),
              "Setting the compression level");

        std::vector<char> compressed(ZSTD_compressBound(src.size()));
        // One spare byte keeps data() non-null for an empty input.
        std::vector<char> decompressed(src.size() + 1);
        const auto compress = [&] {
            return check(
                    ZSTD_compress2(
                            cctx.get(),
                            compressed.data(),
                            compressed.size(),
                            src.data(),
                            src.size()),
                    "Compression");
        };
        const size_t compressedSize = compress();
        const auto decompress       = [&] {
            return check(
                    ZSTD_decompressDCtx(
                            dctx.get(),
                            decompressed.data(),
                            decompressed.size(),
                            compressed.data(),
                            compressedSize),
                    "Decompression");
        };
        if (decompress() != src.size()
            || std::memcmp(decompressed.data(), src.data(), src.size()) != 0) {
            throw std::runtime_error("zstd did not reproduce the input");
        }

        const auto compressionStart = std::chrono::steady_clock::now();
        for (size_t n = 0; n < args.numIters; ++n) {
            if (compress() != compressedSize) {
                throw std::runtime_error("Non-deterministic compression!");
            }
        }
        const auto compressionEnd = std::chrono::steady_clock::now();

        const auto decompressionStart = std::chrono::steady_clock::now();
        for (size_t n = 0; n < args.numIters; ++n) {
            if (decompress() != src.size()) {
                throw std::runtime_error("Round-trip failure!");
            }
        }
        const auto decompressionEnd = std::chrono::steady_clock::now();

        const auto ms = [](auto duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        std::ofstream csv(args.outputCsv);
        csv << "srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters\n"
            << src.size() << "," << compressedSize << ","
            << (compressedSize == 0 ? 0.0
                                    : static_cast<double>(src.size())
                                / static_cast<double>(compressedSize))
            << "," << ms(compressionEnd - compressionStart) << ","
            << ms(decompressionEnd - decompressionStart) << "," << args.numIters
            << "\n";
        if (!csv.flush()) {
            throw std::runtime_error("Cannot write " + args.outputCsv);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
