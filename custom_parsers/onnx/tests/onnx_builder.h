// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openzl::custom_parsers::testing {

/// Writer of protobuf fields.
class ProtoWriter {
   public:
    void varint(uint64_t v)
    {
        while (v >= 0x80) {
            out += (char)(v | 0x80);
            v >>= 7;
        }
        out += (char)v;
    }
    void varintField(uint32_t field, uint64_t v)
    {
        varint((uint64_t)field << 3);
        varint(v);
    }
    void bytesField(uint32_t field, std::string_view bytes)
    {
        varint((uint64_t)field << 3 | 2);
        varint(bytes.size());
        out += bytes;
    }

    std::string out;
};

struct ONNXTensorSpec {
    std::string name;
    uint32_t dataType;
    std::vector<uint64_t> dims; // outermost first
    std::string rawData;
};

/// Serializes a TensorProto, with packed dims as protobuf writes them, or one
/// field per dimension as older writers did.
inline std::string tensorProto(const ONNXTensorSpec& t, bool packedDims = true)
{
    ProtoWriter w;
    if (packedDims && !t.dims.empty()) {
        ProtoWriter dims;
        for (const uint64_t d : t.dims) {
            dims.varint(d);
        }
        w.bytesField(1, dims.out);
    } else {
        for (const uint64_t d : t.dims) {
            w.varintField(1, d);
        }
    }
    w.varintField(2, t.dataType);
    w.bytesField(8, t.name);
    w.bytesField(9, t.rawData);
    return w.out;
}

/// Serializes a NodeProto of type @p opType with one attribute, whose fields
/// after its name and type are @p fields.
inline std::string node(
        std::string_view opType,
        std::string_view attributeName,
        uint32_t attributeType,
        std::string_view fields)
{
    ProtoWriter attribute;
    attribute.bytesField(1, attributeName);
    attribute.out += fields;
    attribute.varintField(20, attributeType);
    ProtoWriter w;
    w.bytesField(2, "out");
    w.bytesField(4, opType);
    w.bytesField(5, attribute.out);
    return w.out;
}

/// A Constant node holding @p value.
inline std::string constantNode(const ONNXTensorSpec& value)
{
    ProtoWriter t;
    t.bytesField(5, tensorProto(value));
    return node("Constant", "value", 4, t.out);
}

/// An If node whose then branch is the GraphProto @p graph.
inline std::string ifNode(const std::string& graph)
{
    ProtoWriter g;
    g.bytesField(6, graph);
    return node("If", "then_branch", 5, g.out);
}

/// Serializes a GraphProto: @p nodes, then @p initializers, then an input and
/// an output, in the order of their field numbers, as protobuf writes them.
inline std::string graphProto(
        const std::vector<std::string>& nodes,
        const std::vector<ONNXTensorSpec>& initializers)
{
    ProtoWriter w;
    for (const auto& n : nodes) {
        w.bytesField(1, n);
    }
    w.bytesField(2, "graph");
    for (const auto& t : initializers) {
        w.bytesField(5, tensorProto(t));
    }
    ProtoWriter input;
    input.bytesField(1, "x");
    w.bytesField(11, input.out);
    ProtoWriter output;
    output.bytesField(1, "y");
    w.bytesField(12, output.out);
    return w.out;
}

/// Serializes a ModelProto around the GraphProto @p graph.
inline std::string buildONNX(const std::string& graph)
{
    ProtoWriter w;
    w.varintField(1, 8); // ir_version
    w.bytesField(2, "openzl-test");
    w.bytesField(7, graph);
    ProtoWriter opset;
    opset.bytesField(1, "");
    opset.varintField(2, 17);
    w.bytesField(8, opset.out);
    return w.out;
}

inline std::string buildONNX(const std::vector<ONNXTensorSpec>& initializers)
{
    return buildONNX(graphProto({}, initializers));
}

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
