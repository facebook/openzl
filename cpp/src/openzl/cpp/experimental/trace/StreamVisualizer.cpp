// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"

#include "openzl/cpp/experimental/trace/CborHelpers.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_errors.h"

namespace openzl::visualizer {
const ZL_Report Stream::serializeStream(
        A1C_Arena* a1c_arena,
        A1C_Item* arrayItem,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MapBuilder builder = A1C_Item_map_builder(arrayItem, 9, a1c_arena);
    ZL_ERR_IF_NULL(builder.map, allocation);

    ZL_ERR_IF_ERR(addIntValue(builder, "chunkId", this->chunkId, opCtx));
    ZL_ERR_IF_ERR(addIntValue(builder, "type", type, opCtx));
    ZL_ERR_IF_ERR(addIntValue(builder, "outputIdx", outputIdx, opCtx));
    ZL_ERR_IF_ERR(addIntValue(builder, "eltWidth", eltWidth, opCtx));
    ZL_ERR_IF_ERR(addIntValue(builder, "numElts", numElts, opCtx));
    ZL_ERR_IF_ERR(addIntValue(builder, "cSize", cSize, opCtx));
    ZL_ERR_IF_ERR(addFloatValue(builder, "share", share, opCtx));
    ZL_ERR_IF_ERR(addIntValue(builder, "contentSize", contentSize, opCtx));

    if (type == ZL_Type_string) {
        ZL_ERR_IF_ERR(addStrArray(
                a1c_arena,
                builder,
                "streamPreview",
                std::get<std::vector<std::string>>(streamPreview),
                opCtx));
    } else if (type == ZL_Type_numeric) {
        ZL_ERR_IF_ERR(addNumArray(
                a1c_arena,
                builder,
                "streamPreview",
                std::get<std::vector<int64_t>>(streamPreview),
                opCtx));
    } else {
        ZL_ERR_IF_ERR(addBytesArray(
                a1c_arena,
                builder,
                "streamPreview",
                std::get<std::vector<uint8_t>>(streamPreview),
                opCtx));
    }

    return ZL_returnSuccess();
}
} // namespace openzl::visualizer
