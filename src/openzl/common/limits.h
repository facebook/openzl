// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMMON_LIMITS_H
#define ZSTRONG_COMMON_LIMITS_H

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/// WARNING: Increasing the encoder limit is potentially format breaking.
/// If it is increased beyond the capacity of an older decoder, then the
/// older decoder will refuse to decode the frame. Similarly decreasing the
/// decoder limit has the same problem.
/// If a decoder limit is increased, the encoder must still use the old
/// limits while encoding for an older format version.

/// ZStrong will refuse to ingest more Inputs than this.
#define ZL_ENCODER_INPUT_LIMIT 2048

/// Since v15, Zstrong can accept multiple inputs.
size_t ZL_runtimeInputLimit(unsigned formatVersion);

/// Since v16, Transforms can accept multiple inputs.
size_t ZL_runtimeNodeInputLimit(unsigned formatVersion);

/// ZStrong will refuse to create graphs that contain more nodes than
/// this. This limit is encoder only, because the decoder doesn't see the
/// static cgraph.
/// NOTE: This limit is encoder only, so it can be increased any time without
/// impacting the format version.
#define ZL_ENCODER_GRAPH_LIMIT 131072

/// ZStrong will refuse to encode/decode graphs that contain more transforms
/// than this.
size_t ZL_runtimeNodeLimit(unsigned formatVersion);

/// ZStrong will refuse to encode/decode graphs that contain more streams than
/// this
size_t ZL_runtimeStreamLimit(unsigned formatVersion);

/// ZStrong will refuse to register more custom nodes than this.
/// NOTE: This limit is encoder only, so it can be increased any time without
/// impacting the format version.
#define ZL_ENCODER_CUSTOM_NODE_LIMIT 4096

/// ZStrong will refuse to encode/decode transforms that contain more than this
/// many outputs.
size_t ZL_transformOutStreamsLimit(unsigned formatVersion);

/// ZStrong will refuse to allocate more space than this to transform headers.
/// NOTE: This limit is encoder only, so it can be increased any time without
/// impacting the format version.
#define ZL_ENCODER_TRANSFORM_HEADER_SIZE_LIMIT 1000000

/// Zstrong will refuse to register more custom transforms than this.
/// NOTE: This limit is part of the frame format, so changing it requires a
/// format version bump, and this should be changed to a function.
#define ZL_CUSTOM_TRANSFORM_LIMIT 10000

/// Default size limit for Zstrong containers (vectors and maps).
/// Should be used sparsely and only when no other limit fits.
/// WARNING: Increasing this limit is potentially format breaking.
/// Carefully consider all use-cases and increase ZL_MIN_FORMAT_VERSION if
/// there's any risk.
#define ZL_CONTAINER_SIZE_LIMIT (1024 * 1024)

/// Size limit for the variable sized comment field
#define ZL_MAX_HEADER_COMMENT_SIZE_LIMIT 10000

////////////////////////////////////////
// Compressor Serialization Limits
////////////////////////////////////////

/// The most params you can have in one param set in a serialized compressor
#define ZL_COMPRESSOR_SERIALIZATION_PARAM_SET_PARAM_LIMIT 1024

/// How many param sets you can have in a serialized compressor
#define ZL_COMPRESSOR_SERIALIZATION_PARAM_SET_LIMIT 1024

/// How many graphs you can have in a serialized compressor
#define ZL_COMPRESSOR_SERIALIZATION_GRAPH_COUNT_LIMIT ZL_ENCODER_GRAPH_LIMIT

/// How many nodes you can have in a serialized compressor
#define ZL_COMPRESSOR_SERIALIZATION_NODE_COUNT_LIMIT \
    ZL_ENCODER_CUSTOM_NODE_LIMIT

/// How many custom graphs a graph can list as successors in a serialized
/// compressor
#define ZL_COMPRESSOR_SERIALIZATION_GRAPH_CUSTOM_GRAPH_LIMIT 1024

/// How many nodes a graph can list as successors in a serialized compressor
#define ZL_COMPRESSOR_SERIALIZATION_GRAPH_CUSTOM_NODE_LIMIT 1024

////////////////////////////////////////
// Simple Data Description Language Limits
////////////////////////////////////////

/// How many tokens a parse can decompose into
#define ZL_SDDL_SEGMENT_LIMIT 100000000

/// How many variables a parse can declare
#define ZL_SDDL_VARIABLE_LIMIT 1024

/// How many dests a parse can declare
#define ZL_SDDL_DEST_LIMIT 1024

ZL_END_C_DECLS

#endif
