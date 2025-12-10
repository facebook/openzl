#include "openzl/codecs/lz4/decode_lz4_binding.h"
#include <limits.h>
#include "openzl/common/assertion.h"
#include "openzl/shared/varint.h"

#ifndef LZ4_STATIC_LINKING_ONLY
#    define LZ4_STATIC_LINKING_ONLY
#endif
#include <lz4.h>

ZL_Report DI_lz4(ZL_Decoder* dic, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dic);
    ZL_ASSERT_NN(dic);
    ZL_ASSERT_NN(ins);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);
    ZL_ERR_IF_GT(inSize, INT_MAX, node_invalid_input);

    // Read the original size from the header
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dic);
    ZL_ERR_IF_EQ(header.size, 0, GENERIC, "No header provided");
    const uint8_t* headerStart = (const uint8_t*)header.start;
    const uint8_t* headerEnd   = (const uint8_t*)header.start + header.size;
    ZL_TRY_LET_CONST(
            uint64_t, outSize, ZL_varintDecode(&headerStart, headerEnd));
    ZL_ERR_IF_GT(outSize, INT_MAX, node_invalid_input);

    // Allocate the output buffer
    ZL_Output* const out = ZL_Decoder_createTypedStream(dic, 0, outSize, 1);
    ZL_ERR_IF_NULL(out, allocation);

    // Do the decompression
    int consumed = LZ4_decompress_safe(
            (const char*)ZL_Input_ptr(in),
            (char*)ZL_Output_ptr(out),
            (int)inSize,
            (int)outSize);
    ZL_ERR_IF_NE(consumed, outSize, GENERIC, "LZ4_decompress_safe failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, (size_t)consumed));

    return ZL_returnSuccess();
}
