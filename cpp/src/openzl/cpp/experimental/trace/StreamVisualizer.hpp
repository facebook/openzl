// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/experimental/trace/types.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_opaque_types.h"

#include <functional>
#include <optional>
#include <vector>

namespace openzl::visualizer {

struct Stream {
    StreamID id{};
    ZL_Type type{};
    size_t outputIdx{};
    size_t eltWidth{};
    size_t numElts{};
    size_t cSize{};
    double share{};
    size_t contentSize{};
    size_t chunkId{};
    std::vector<StreamID> successors;
    std::optional<CodecID> consumerCodec;
    std::optional<CodecID> producerCodec;
    StreamPreview streamPreview;

    const ZL_Report serializeStream(A1C_Arena* a1c_arena, A1C_Item* arrayItem);
};

// custom operators for maps using ZL_DataID as a key to identify streams
struct ZL_DataIDCustomComparator {
    bool operator()(const ZL_DataID& lhs, const ZL_DataID& rhs) const
    {
        return lhs.sid < rhs.sid;
    }
};

struct ZL_DataIDHash {
    size_t operator()(const ZL_DataID& dataID) const
    {
        return std::hash<ZL_IDType>{}(dataID.sid);
    }
};

struct ZL_DataIDEquality {
    bool operator()(const ZL_DataID& lhs, const ZL_DataID& rhs) const
    {
        return lhs.sid == rhs.sid;
    }
};

} // namespace openzl::visualizer
