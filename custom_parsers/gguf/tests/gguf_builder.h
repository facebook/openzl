// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openzl::custom_parsers::testing {

struct GGUFTensorSpec {
    std::string name;
    uint32_t type;
    std::vector<uint64_t> ne; // innermost dimension first
    std::string payload;
    std::optional<uint64_t> offset = std::nullopt; // replaces the computed one
};

/// Little-endian writer of GGUF fields.
class GGUFWriter {
   public:
    void u32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            out += (char)(v >> (8 * i));
        }
    }
    void u64(uint64_t v)
    {
        for (int i = 0; i < 8; ++i) {
            out += (char)(v >> (8 * i));
        }
    }
    void str(const std::string& s)
    {
        u64(s.size());
        out += s;
    }
    void header(uint32_t version, uint64_t nbTensors, uint64_t nbMetadata)
    {
        out += "GGUF";
        u32(version);
        u64(nbTensors);
        u64(nbMetadata);
    }

    std::string out;
};

/// Number of key/value pairs written by writeSampleMetadata().
inline constexpr uint64_t kSampleMetadataCount = 4;

/// Metadata of the main value types, including nested arrays.
inline void writeSampleMetadata(GGUFWriter& w)
{
    w.str("general.architecture");
    w.u32(8); // string
    w.str("llama");
    w.str("general.file_type");
    w.u32(10); // u64
    w.u64(7);
    w.str("tokenizer.ggml.tokens");
    w.u32(9); // array
    w.u32(8); // of strings
    w.u64(2);
    w.str("a");
    w.str("bc");
    w.str("test.nested");
    w.u32(9); // array
    w.u32(9); // of arrays
    w.u64(2);
    for (int i = 0; i < 2; ++i) {
        w.u32(7); // of bools
        w.u64(3);
        w.out += std::string(3, '\1');
    }
}

/// Serialized metadata key/value pairs.
struct GGUFMetadata {
    uint64_t count;
    std::string bytes;
};

inline GGUFMetadata sampleMetadata()
{
    GGUFWriter w;
    writeSampleMetadata(w);
    return { kSampleMetadataCount, w.out };
}

/// Builds a GGUF file: tensor data are placed in order, each aligned.
/// general.alignment follows @p metadata when @p alignment is not 32.
/// Tensor infos follow @p infoOrder, a permutation of the tensor indices, when
/// it is not empty.
inline std::string buildGGUF(
        const std::vector<GGUFTensorSpec>& tensors,
        uint32_t alignment                   = 32,
        const GGUFMetadata& metadata         = sampleMetadata(),
        const std::vector<size_t>& infoOrder = {})
{
    assert(alignment != 0 && (alignment & (alignment - 1)) == 0);
    assert(infoOrder.empty() || infoOrder.size() == tensors.size());
    GGUFWriter w;
    const bool customAlignment = alignment != 32;
    w.header(3, tensors.size(), metadata.count + customAlignment);
    w.out += metadata.bytes;
    if (customAlignment) {
        w.str("general.alignment");
        w.u32(4); // u32
        w.u32(alignment);
    }
    std::string data;
    std::vector<uint64_t> offsets;
    for (const auto& t : tensors) {
        data.append((alignment - data.size() % alignment) % alignment, '\0');
        offsets.push_back(t.offset.value_or(data.size()));
        data += t.payload;
    }
    for (size_t i = 0; i < tensors.size(); ++i) {
        const size_t n = infoOrder.empty() ? i : infoOrder[i];
        const auto& t  = tensors[n];
        w.str(t.name);
        w.u32((uint32_t)t.ne.size());
        for (const auto dim : t.ne) {
            w.u64(dim);
        }
        w.u32(t.type);
        w.u64(offsets[n]);
    }
    w.out.append((alignment - w.out.size() % alignment) % alignment, '\0');
    return w.out + data;
}

/// Deterministic pseudo-random bytes.
inline std::string pseudoRandomBytes(size_t size, uint32_t seed)
{
    std::string out(size, '\0');
    uint32_t state = seed * 2654435761u + 1;
    for (auto& c : out) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        c = (char)state;
    }
    return out;
}

} // namespace openzl::custom_parsers::testing
