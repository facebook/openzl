#include "openzl/codecs/lz4/encode_lz4_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardNodeID_lz4
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_errors_types.h"
#include "openzl/zl_localParams.h"

#ifndef LZ4_STATIC_LINKING_ONLY
#    define LZ4_STATIC_LINKING_ONLY
#endif
#include <lz4.h>
#include <lz4hc.h>

ZL_Report EI_lz4(ZL_Encoder* eic, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eic);
    ZL_ASSERT_NN(eic);
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbIns, 1);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);
    ZL_ERR_IF_GT(inSize, LZ4_MAX_INPUT_SIZE, node_invalid_input);

    // By default use the global compression level
    int cLevel = ZL_Encoder_getCParam(eic, ZL_CParam_compressionLevel);

    // Get the compression level override, if it exists
    ZL_IntParam cLevelParam = ZL_Encoder_getLocalIntParam(
            eic, ZL_LZ4_COMPRESSION_LEVEL_OVERRIDE_PID);
    if (cLevelParam.paramId == ZL_LZ4_COMPRESSION_LEVEL_OVERRIDE_PID) {
        cLevel = cLevelParam.paramValue;
    }

    // Allocate the output buffer
    int outSize = LZ4_compressBound((int)inSize);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eic, 0, (size_t)outSize, 1);
    ZL_ERR_IF_NULL(out, allocation);

    // Do the compression
    int compressedSize;
    if (cLevel <= 1) {
        const int acceleration = (cLevel < 0) ? -cLevel + 1 : 1;
        compressedSize         = LZ4_compress_fast(
                (const char*)ZL_Input_ptr(in),
                (char*)ZL_Output_ptr(out),
                (int)inSize,
                outSize,
                acceleration);
    } else {
        compressedSize = LZ4_compress_HC(
                (const char*)ZL_Input_ptr(in),
                (char*)ZL_Output_ptr(out),
                (int)inSize,
                outSize,
                cLevel);
    }
    ZL_ERR_IF_LE(compressedSize, 0, GENERIC, "LZ4_compress_default failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, (size_t)compressedSize));

    // Write the original size as a varint
    uint8_t header[ZL_VARINT_LENGTH_32];
    size_t headerSize = ZL_varintEncode((uint64_t)inSize, header);
    ZL_Encoder_sendCodecHeader(eic, header, headerSize);

    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildLZ4Graph(ZL_Compressor* compressor, int compressionLevel)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);
    ZL_IntParam intParam = { ZL_LZ4_COMPRESSION_LEVEL_OVERRIDE_PID,
                             compressionLevel };

    ZL_LocalParams localParams = {
        .intParams = { &intParam, 1 },
    };
    ZL_GraphParameters desc = {
        .localParams = &localParams,
    };
    return ZL_Compressor_parameterizeGraph(compressor, ZL_GRAPH_LZ4, &desc);
}
