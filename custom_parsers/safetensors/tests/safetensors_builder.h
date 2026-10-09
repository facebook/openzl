// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openzl::custom_parsers::testing {

struct TensorSpec {
    std::string name;
    std::string dtype;
    std::vector<uint64_t> shape;
    std::string payload;
    size_t gapBefore = 0; // zero bytes inserted before the payload
};

/// Builds a safetensors file. Members are written in the given order.
inline std::string buildSafetensors(
        const std::vector<TensorSpec>& tensors,
        const std::string& metadataJson = "")
{
    std::string json = "{";
    std::string data;
    if (!metadataJson.empty()) {
        json += "\"__metadata__\":" + metadataJson;
    }
    for (const auto& t : tensors) {
        data.append(t.gapBefore, '\0');
        const size_t begin = data.size();
        data += t.payload;
        if (json.size() > 1) {
            json += ",";
        }
        json += "\"" + t.name + "\":{\"dtype\":\"" + t.dtype + "\",\"shape\":[";
        for (size_t d = 0; d < t.shape.size(); ++d) {
            json += (d ? "," : "") + std::to_string(t.shape[d]);
        }
        json += "],\"data_offsets\":[" + std::to_string(begin) + ","
                + std::to_string(data.size()) + "]}";
    }
    json += "}";
    json.append((8 - json.size() % 8) % 8, ' ');
    std::string out(8, '\0');
    uint64_t size = json.size();
    for (size_t i = 0; i < 8; ++i) {
        out[i] = (char)(size >> (8 * i));
    }
    return out + json + data;
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
