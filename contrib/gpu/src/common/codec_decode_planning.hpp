// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "openzl/common/wire_format.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

namespace openzl::gpu {

/**
 * Logical properties of a decoded stream, independent of its storage.
 *
 * Fixed-width streams use @c numElts and @c eltWidth to describe their
 * logical layout. Serial streams count bytes as elements, while string
 * streams count strings and have an element width of zero.
 */
struct StreamShape {
    /// Logical OpenZL representation of the stream contents.
    ZL_Type type;
    /// Logical element count; for serial streams, this is the byte count.
    size_t numElts;
    /// Bytes per fixed-width element, or zero for string streams.
    size_t eltWidth;
};

/**
 * Byte extents of the separately addressable regions backing a stream.
 *
 * These extents describe either storage already available to an input or
 * storage required by an output. They do not identify an allocation or imply
 * ownership.
 */
struct StreamStorageSize {
    /// Bytes in the primary data region; strings store concatenated bytes.
    size_t dataBytes;
    /// Bytes in the string-length table, or zero for non-string streams.
    size_t stringLengthsBytes;
};

/**
 * Host-side metadata available for one decoder input during planning.
 *
 * Entries are supplied in decoder argument order. The planner can inspect the
 * logical shape and validate byte requirements against @c availableStorage,
 * but it cannot access the input allocation or payload through this type.
 */
struct StreamPlanningInput {
    /// Logical type and element layout visible to the decoder.
    StreamShape shape{};
    /// Upper bounds on the input regions that a decoder may read.
    StreamStorageSize availableStorage{};
};

/**
 * Selects an input data region that may back a regenerated output.
 *
 * The output's required @c dataBytes begin at @c offsetBytes in the selected
 * input and must fit within that input's available primary data region.
 */
struct StreamInputAlias {
    /// Index into the codec inputs, which are in decoder argument order.
    size_t inputIndex;
    /// Byte offset from the start of the selected input's primary data region.
    size_t offsetBytes;
};

/**
 * Logical layout and storage plan for one regenerated decoder output.
 *
 * @c storageSize is the required output footprint. It sizes a new allocation
 * when @c alias is absent and bounds the selected input slice when @c alias is
 * present.
 */
struct CodecDecodeOutputPlan {
    /// Logical type and element layout the decoder will produce.
    StreamShape shape{};
    /// Required byte extents for the output's addressable regions.
    StreamStorageSize storageSize{};
    /// Input-backed data region, or nullopt when storage must be materialized.
    std::optional<StreamInputAlias> alias;
};

/**
 * Immutable wire metadata for planning one codec node in a prepared chunk.
 *
 * The codec header is a non-owning host span. Its backing storage must remain
 * valid for the planning call.
 */
struct CodecDecodePlanningContext {
    /// Exact transform namespace and ID decoded from the frame node.
    PublicTransformInfo transform;
    /// Frame format version governing the transform and header semantics.
    uint32_t frameFormatVersion;
    /// Host-readable codec-private transform header for this node.
    std::span<const std::byte> codecHeader_h;
};

/**
 * Plans every regenerated output of one codec node without reading payloads.
 *
 * @param codec Wire metadata for the node being planned.
 * @param inputs Input metadata in decoder argument order.
 * @param outputs Caller-sized destinations in regenerated-stream order.
 * @param opCtx Operation context used to preserve OpenZL error details.
 * @returns A success report carrying the number of planned outputs, or the
 * first validation or allocation-sizing error.
 */
using CodecDecodePlanningFn = ZL_Report(
        const CodecDecodePlanningContext& codec,
        std::span<const StreamPlanningInput> inputs,
        std::span<CodecDecodeOutputPlan> outputs,
        ZL_OperationContext* opCtx);

} // namespace openzl::gpu
