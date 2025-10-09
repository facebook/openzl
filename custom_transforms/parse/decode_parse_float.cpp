// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/errors_internal.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

#include <cstring>

#include <folly/Conv.h>

namespace zstrong {
namespace {

size_t constexpr maxStrLen(double)
{
    return 26;
}

class StreamAppender {
   public:
    explicit StreamAppender(ZL_Output* stream)
            : buffer_((char*)ZL_Output_ptr(stream))
    {
    }

    void append(char const* ptr, size_t length)
    {
        std::memcpy(buffer_ + idx_, ptr, length);
        idx_ += length;
    }

    void push_back(char c)
    {
        buffer_[idx_++] = c;
    }

    size_t commitField()
    {
        size_t const fieldSize = idx_ - prev_;
        prev_                  = idx_;
        return fieldSize;
    }

   private:
    char* buffer_;
    size_t idx_{ 0 };
    size_t prev_{ 0 };
};
} // namespace
} // namespace zstrong

namespace folly {
template <>
struct IsSomeString<zstrong::StreamAppender> : std::true_type {};
} // namespace folly

namespace zstrong {
namespace {
template <typename T>
ZL_Report parseDecode(ZL_Decoder* dictx, ZL_Input const* inputs[]) noexcept
{
    ZL_Input const* numbers          = inputs[0];
    ZL_Input const* exceptionIndices = inputs[1];
    ZL_Input const* exceptions       = inputs[2];

    ZL_RET_R_IF_NE(
            corruption,
            ZL_Input_numElts(exceptionIndices),
            ZL_Input_numElts(exceptions));
    ZL_RET_R_IF_NE(corruption, ZL_Input_eltWidth(exceptionIndices), 4);
    ZL_RET_R_IF_NE(corruption, ZL_Input_eltWidth(numbers), sizeof(T));

    size_t const outBound = ZL_Input_contentSize(exceptions)
            + ZL_Input_numElts(numbers) * maxStrLen(T{});
    ZL_Output* outStream = ZL_Decoder_create1OutStream(dictx, outBound, 1);
    ZL_RET_R_IF_NULL(allocation, outStream);

    StreamAppender outAppender{ outStream };

    size_t const nbElts =
            ZL_Input_numElts(numbers) + ZL_Input_numElts(exceptions);

    uint32_t* fieldSizes = ZL_Output_reserveStringLens(outStream, nbElts);
    ZL_RET_R_IF_NULL(allocation, fieldSizes);

    auto nums          = (T const*)ZL_Input_ptr(numbers);
    auto const numsEnd = nums + ZL_Input_numElts(numbers);

    auto exIdxs          = (uint32_t const*)ZL_Input_ptr(exceptionIndices);
    auto const exIdxsEnd = exIdxs + ZL_Input_numElts(exceptionIndices);

    auto exData  = (char const*)ZL_Input_ptr(exceptions);
    auto exSizes = ZL_Input_stringLens(exceptions);

    for (size_t i = 0; i < nbElts; ++i) {
        if (exIdxs < exIdxsEnd && i == *exIdxs) {
            ++exIdxs;
            auto const exSize = *exSizes++;
            outAppender.append(exData, exSize);
            exData += exSize;
        } else {
            ZL_RET_R_IF_EQ(srcSize_tooSmall, nums, numsEnd);
            folly::toAppend(*nums++, &outAppender);
        }
        fieldSizes[i] = outAppender.commitField();
    }

    ZL_RET_R_IF_NE(corruption, nums, numsEnd);
    ZL_RET_R_IF_NE(corruption, exIdxs, exIdxsEnd);

    ZL_RET_R_IF_ERR(ZL_Output_commit(outStream, nbElts));

    return ZL_returnSuccess();
}

} // namespace
} // namespace zstrong

extern "C" ZL_Report ZL_DCtx_registerParseFloat64(
        ZL_DCtx* dctx,
        ZL_IDType transformID)
{
    std::array<ZL_Type, 3> const kOutStreams = {
        ZL_Type_numeric,
        ZL_Type_numeric,
        ZL_Type_string,
    };
    ZL_TypedGraphDesc graph = {
        .CTid           = transformID,
        .inStreamType   = ZL_Type_string,
        .outStreamTypes = kOutStreams.data(),
        .nbOutStreams   = kOutStreams.size(),
    };
    ZL_TypedDecoderDesc desc = {
        .gd          = graph,
        .transform_f = zstrong::parseDecode<double>,
        .name        = "parse float64",
    };
    return ZL_DCtx_registerTypedDecoder(dctx, &desc);
}
