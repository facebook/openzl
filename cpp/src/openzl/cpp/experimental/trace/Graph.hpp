// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/experimental/trace/types.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

#include <string>
#include <vector>

namespace openzl::visualizer {

struct Graph {
    ZL_GraphType gType{};
    const char* gName{};
    ZL_Report gFailure = ZL_returnSuccess();
    std::string gFailureString;
    LocalParams gLocalParams{};
    size_t chunkId{};

    // temporary hack to report failed graphs that have no codecs
    // ultimately, the idea is that edges will go to graphs and not codecs so
    // there's no need to store this info
    std::vector<ZL_Edge*> inEdges;
    std::vector<CodecID> codecs;

    const ZL_Report serializeGraph(A1C_Arena* a1c_arena, A1C_Item* arrayItem);
};

} // namespace openzl::visualizer
