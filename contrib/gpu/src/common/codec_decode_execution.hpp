// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contrib/gpu/src/common/codec_decode_planning.hpp"
#include "openzl/common/wire_format.h"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/zl_errors.h"

namespace openzl::gpu {

/** One decoder input, as a kernel sees it before it has an address. */
struct InputStreamInfo {
    /// Logical type and element layout of the input.
    StreamShape shape{};
    /// Bytes readable in the input's data and string-length regions.
    StreamStorageSize availableStorage{};
};

/** One decoder output, as a kernel sees it before it has an address. */
struct OutputStreamInfo {
    /// Logical type and element layout the decoder must produce.
    StreamShape shape{};
    /// Bytes writable in the output's data and string-length regions.
    StreamStorageSize storageSize{};
};

/**
 * One planned codec run, without addresses.
 *
 * Binding chooses each run's kernel from this info, before any memory is
 * allocated. Every span is non-owning host memory. Inputs are in decoder
 * argument order, and outputs are in regenerated-stream order, as in
 * @c PlannedCodecRun.
 */
struct CodecRunInfo {
    /// Exact transform namespace and ID decoded from the frame node.
    PublicTransformInfo transform{};
    /// Frame format version governing the transform and header semantics.
    uint32_t frameFormatVersion = 0;
    /// Host-readable codec-private transform header for this node.
    poly::span<const std::byte> codecHeader_h;
    poly::span<const InputStreamInfo> inputs;
    poly::span<const OutputStreamInfo> outputs;
};

/**
 * The device addresses of one decoder input.
 *
 * The address space is the device's: a host device's addresses are host
 * addresses.
 */
struct BoundInputStream {
    /// Beginning of the primary data region.
    const void* data_d = nullptr;
    /// Beginning of the string-length table, or nullptr for non-string streams.
    const uint32_t* stringLengths_d = nullptr;
};

/** The device addresses of one decoder output. */
struct BoundOutputStream {
    /// Beginning of the primary data region.
    void* data_d = nullptr;
    /// Beginning of the string-length table, or nullptr for non-string streams.
    uint32_t* stringLengths_d = nullptr;
};

/**
 * One codec run with device addresses for all its streams.
 *
 * @c inputs[i] and @c outputs[i] hold the addresses of @c info.inputs[i] and
 * @c info.outputs[i]. A bound run holds all the info that binding chose its
 * kernel by.
 */
struct BoundCodecRun {
    CodecRunInfo info;
    poly::span<const BoundInputStream> inputs;
    poly::span<const BoundOutputStream> outputs;
};

/**
 * One implementation of a codec on one device runtime.
 *
 * A device lists the kernels of each codec in its table, in order of
 * preference. A codec can have several: kernels for different variants, such
 * as element types, or alternatives for the same runs, such as a vendor
 * library. The first kernel that accepts a run decodes it.
 */
struct DecodeKernel {
    /// Stable name for errors, benchmarks, and test output.
    const char* name;
    /**
     * Whether this kernel can decode @p run, from its header, shapes, sizes,
     * and staged bytes. Binding calls it before any memory exists.
     */
    bool (*accepts)(const CodecRunInfo& run);
    /**
     * Enqueues the decode of every run in @p runs on the runtime's native
     * @p stream, which is current, and returns without waiting for the device.
     * Every run was accepted. @p runs may be reused when this returns.
     */
    ZL_Report (*decode)(poly::span<const BoundCodecRun> runs, void* stream);
};

/**
 * Runs of one wave that one kernel decodes in one call.
 *
 * The work schedule puts each run in a wave after its producers, and the runs
 * of one transform in a wave form a work group. A kernel decodes one codec, so
 * a batch never mixes work groups. A work group whose runs need different
 * kernels, such as two element types, splits into one batch for each kernel.
 */
struct DecodeBatch {
    const DecodeKernel* kernel = nullptr;
    /// Indexes of the batch's runs among the runs that were batched, in
    /// their order.
    std::vector<size_t> runs;
};

/**
 * One device's kernels for each standard codec, indexed by standard transform
 * ID. Each list is in the device's order of preference. A device leaves out
 * the kernels that cannot run in this process.
 */
using DecodeKernelTable =
        std::array<std::vector<DecodeKernel>, ZL_StandardTransformID_end>;

} // namespace openzl::gpu
