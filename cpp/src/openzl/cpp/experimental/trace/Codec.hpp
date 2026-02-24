// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/experimental/trace/types.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

#include <string>
#include <vector>

namespace openzl::visualizer {

struct Codec {
    std::string name;
    bool cType;
    ZL_IDType cID{};
    size_t cHeaderSize{};
    ZL_Report cFailure = ZL_returnSuccess();
    LocalParams cLocalParams{};
    size_t chunkId{};
    size_t codecNum{};
    std::vector<StreamID> inEdges;
    std::vector<StreamID> outEdges;

    const ZL_Report serializeCodec(
            A1C_Arena* a1c_arena,
            A1C_Item* arrayItem,
            const ZL_CCtx* const cctx);
};

} // namespace openzl::visualizer
