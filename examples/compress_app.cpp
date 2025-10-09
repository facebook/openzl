// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <fstream>
#include <iostream>
#include <sstream>

#include "openzl/common/assertion.h" // ZS_REQUIRE_,
#include "openzl/zl_compressor.h" // ZL_Compressor_create, ZL_Compressor_compress, ZL_Compressor_compressBound

/* The format version used when compressing in the example.  */
#define ZSTRONG_EXAMPLE_FORMAT_VERSION (16)

// Move to utils.cpp file
static std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Move to utils.cpp file
static void throwOnCompressionError(ZL_Report result)
{
    std::stringstream ss;
    if (ZL_isError(result)) {
        ss << "compression failed: "
           /* Get the error string associated with the compress failure */
           << ZL_ErrorCode_toString(ZL_errorCode(result)) << std::endl;
        throw std::runtime_error(ss.str());
    }
}

static size_t compress(
        void* dstBuff,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphID gid)
{
    /* Create cctx to manage compression state */
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);
    /* Create cgraph to store the compression graph */
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    /* Sets the format verion and checks it is successful. Decompression is
    valid only when using a zstrong version equal to or higher than the
    compressor's version number. Catches errors ... */
    throwOnCompressionError(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZSTRONG_EXAMPLE_FORMAT_VERSION));
    /* Passes the starting compression graph to the cgraph. Catches errors ...
     */
    throwOnCompressionError(ZL_Compressor_selectStartingGraphID(cgraph, gid));
    /* Reference the cgraph to the compression state. Catches erros where ... */
    throwOnCompressionError(ZL_CCtx_refCompressor(cctx, cgraph));
    /* Compresses the data and checks it is successful, returning the compressed
     * size. */
    ZL_Report result =
            ZL_CCtx_compress(cctx, dstBuff, dstCapacity, src, srcSize);
    /* Checks for errors that occur and reports it while compressing. Catches
     * any format restrictions on the input*/
    throwOnCompressionError(result);
    /* Free the cgraph */
    ZL_Compressor_free(cgraph);
    /* free cctx*/
    ZL_CCtx_free(cctx);
    return ZL_validResult(result);
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input_path>" << "<output_path>"
                  << std::endl;
        return 1;
    }
    std::string inputPath(argv[1]);
    std::string outputPath(argv[2]);

    std::string rawInput = readFile(inputPath);

    /* Calculate the maximum compressed size of the input */
    size_t const compressedBound = ZL_compressBound(rawInput.size());
    /* Allocate memory for the destination buffer */
    void* const dstBuff = malloc(compressedBound);
    ZL_REQUIRE_NN(dstBuff);
    size_t compressedSize = compress(
            dstBuff,
            compressedBound,
            rawInput.data(),
            rawInput.size(),
            ZL_GRAPH_ZSTD);
    free(dstBuff);
    std::cout << "uncompressed size: " << (float)rawInput.size() << " bytes\n"
              << "compressed size: " << compressedSize << " bytes\n"
              << "compression ratio: "
              << (float)rawInput.size() / compressedSize << std::endl;

    return 0;
}
