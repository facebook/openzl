// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <optional>

#include "contrib/gpu/src/codecs/float_deconstruct/float_deconstruct_bf16.hpp"
#include "contrib/gpu/src/common/codec_decode_execution.hpp"

namespace openzl::gpu {

/**
 * Whether @p run is a bf16 float_deconstruct run with consistent shapes and
 * enough storage for its element count.
 */
bool isBf16CodecRun(const CodecRunInfo& run);

/**
 * Describes the arrays of @p run for the bf16 kernel.
 *
 * @returns The descriptor, or no value if @p run fails @c isBf16CodecRun, or
 * has elements and a null address.
 */
std::optional<Bf16DeconElementRange> asBf16CodecRun(const BoundCodecRun& run);

} // namespace openzl::gpu
