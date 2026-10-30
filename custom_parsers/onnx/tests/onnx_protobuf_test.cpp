// Copyright (c) Meta Platforms, Inc. and affiliates.

// Checks the lexer against models serialized by the protobuf library, with
// the message definitions of ONNX.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "custom_parsers/onnx/onnx_lexer.h"
#include "onnx/onnx_pb.h"

namespace openzl::custom_parsers {
namespace {

struct Tensor {
    std::string name;
    uint32_t dataType;
    std::vector<uint64_t> dims;
    std::string rawData;

    bool operator==(const Tensor&) const = default;
};

/// Element sizes in bits, from the comments of onnx.proto, 0 for strings and
/// unknown types.
uint64_t elementBits(int dataType)
{
    switch (dataType) {
        case onnx::TensorProto::UINT2:
        case onnx::TensorProto::INT2:
            return 2;
        case onnx::TensorProto::UINT4:
        case onnx::TensorProto::INT4:
        case onnx::TensorProto::FLOAT4E2M1:
            return 4;
        case onnx::TensorProto::UINT8:
        case onnx::TensorProto::INT8:
        case onnx::TensorProto::BOOL:
        case onnx::TensorProto::FLOAT8E4M3FN:
        case onnx::TensorProto::FLOAT8E4M3FNUZ:
        case onnx::TensorProto::FLOAT8E5M2:
        case onnx::TensorProto::FLOAT8E5M2FNUZ:
        case onnx::TensorProto::FLOAT8E8M0:
            return 8;
        case onnx::TensorProto::UINT16:
        case onnx::TensorProto::INT16:
        case onnx::TensorProto::FLOAT16:
        case onnx::TensorProto::BFLOAT16:
            return 16;
        case onnx::TensorProto::FLOAT:
        case onnx::TensorProto::INT32:
        case onnx::TensorProto::UINT32:
            return 32;
        case onnx::TensorProto::INT64:
        case onnx::TensorProto::UINT64:
        case onnx::TensorProto::DOUBLE:
        case onnx::TensorProto::COMPLEX64:
            return 64;
        case onnx::TensorProto::COMPLEX128:
            return 128;
        default:
            return 0;
    }
}

class RandomModel {
   public:
    explicit RandomModel(uint32_t seed) : rng_(seed) {}

    /// Builds a model, recording the tensors the lexer should find, in file
    /// order.
    std::string build()
    {
        onnx::ModelProto model;
        model.set_ir_version(10);
        model.set_producer_name("openzl-test");
        fillGraph(*model.mutable_graph(), 0);
        model.add_opset_import()->set_version(21);
        std::string file;
        EXPECT_TRUE(model.SerializeToString(&file));
        return file;
    }

    std::vector<Tensor> expected;

   private:
    size_t pick(size_t n)
    {
        return std::uniform_int_distribution<size_t>(0, n - 1)(rng_);
    }

    /// A tensor of any type, including unknown ones, whose raw data mostly
    /// matches its dims. Fields are set in random order: protobuf serializes
    /// them in the order of their numbers anyway.
    void fillTensor(onnx::TensorProto& t)
    {
        const int dataType = (int)pick(28);
        t.set_data_type(dataType);
        t.set_name("t" + std::to_string(nbTensors_++));
        uint64_t nbElts    = 1;
        const size_t ndims = pick(5) + (pick(20) == 0 ? ZL_ONNX_MAX_DIMS : 0);
        for (size_t i = 0; i < ndims; ++i) {
            const uint64_t dim = pick(i < 4 ? 30 : 2);
            t.add_dims((int64_t)dim);
            nbElts *= dim;
        }
        const uint64_t bits = elementBits(dataType);
        size_t nbBytes      = (size_t)((nbElts * (bits ? bits : 8) + 7) / 8);
        if (pick(8) == 0) {
            nbBytes = pick(200);
        }
        std::string raw(nbBytes, '\0');
        for (auto& c : raw) {
            c = (char)pick(256);
        }
        switch (pick(10)) {
            case 0: // typed field, no raw_data
                t.add_float_data(1.5f);
                return;
            case 1: // external data
                t.set_data_location(onnx::TensorProto::EXTERNAL);
                t.add_external_data()->set_key("location");
                return;
            default:
                t.set_raw_data(raw);
                break;
        }
        if (raw.empty()) {
            return;
        }
        uint64_t expectedBytes = 0;
        bool consistent        = bits != 0 && ndims <= ZL_ONNX_MAX_DIMS;
        if (consistent) {
            expectedBytes = (nbElts * bits + 7) / 8;
            consistent    = expectedBytes == raw.size();
        }
        std::vector<uint64_t> dims(t.dims().begin(), t.dims().end());
        dims.resize(std::min(dims.size(), (size_t)ZL_ONNX_MAX_DIMS));
        expected.push_back(
                { t.name(),
                  consistent ? (uint32_t)dataType : 0u,
                  std::move(dims),
                  raw });
    }

    void fillSparse(onnx::SparseTensorProto& s)
    {
        fillTensor(*s.mutable_values());
        fillTensor(*s.mutable_indices());
    }

    /// Nodes come first in the file, then initializers, then sparse ones.
    void fillGraph(onnx::GraphProto& g, int depth)
    {
        g.set_name("g");
        const size_t nbNodes = pick(depth == 0 ? 6 : 3);
        for (size_t i = 0; i < nbNodes; ++i) {
            auto& node = *g.add_node();
            node.set_op_type("Op");
            node.add_input("x");
            auto& a = *node.add_attribute();
            a.set_name("a");
            switch (pick(4)) {
                case 0:
                    a.set_type(onnx::AttributeProto::TENSOR);
                    fillTensor(*a.mutable_t());
                    break;
                case 1:
                    a.set_type(onnx::AttributeProto::TENSORS);
                    fillTensor(*a.add_tensors());
                    fillTensor(*a.add_tensors());
                    break;
                case 2:
                    a.set_type(onnx::AttributeProto::SPARSE_TENSOR);
                    fillSparse(*a.mutable_sparse_tensor());
                    break;
                default:
                    a.set_type(onnx::AttributeProto::GRAPH);
                    if (depth < ZL_ONNX_MAX_GRAPH_DEPTH) {
                        fillGraph(*a.mutable_g(), depth + 1);
                    }
                    break;
            }
        }
        const size_t nbInitializers = pick(depth == 0 ? 10 : 3);
        for (size_t i = 0; i < nbInitializers; ++i) {
            fillTensor(*g.add_initializer());
        }
        if (pick(4) == 0) {
            fillSparse(*g.add_sparse_initializer());
        }
        g.add_input()->set_name("x");
        g.add_output()->set_name("y");
    }

    std::mt19937 rng_;
    size_t nbTensors_ = 0;
};

TEST(ONNXProtobufTest, LexesTheTensorsOfModelsSerializedByProtobuf)
{
    for (uint32_t seed = 0; seed < 500; ++seed) {
        RandomModel model(seed);
        const std::string file = model.build();

        const ZL_Report maxTokens =
                ZL_ONNX_lex(file.data(), file.size(), nullptr, 0, nullptr);
        ASSERT_FALSE(ZL_isError(maxTokens)) << "seed " << seed;
        std::vector<ZL_ONNXToken> tokens(ZL_validResult(maxTokens));
        const ZL_Report nbTokens = ZL_ONNX_lex(
                file.data(),
                file.size(),
                tokens.data(),
                tokens.size(),
                nullptr);
        ASSERT_FALSE(ZL_isError(nbTokens)) << "seed " << seed;
        tokens.resize(ZL_validResult(nbTokens));

        std::vector<Tensor> lexed;
        size_t covered = 0;
        for (const auto& t : tokens) {
            ASSERT_EQ(t.ptr, file.data() + covered) << "seed " << seed;
            covered += t.size;
            if (t.type == ZL_ONNXTokenType_tensor) {
                lexed.push_back(
                        { std::string(t.name, t.nameSize),
                          t.dataType,
                          { t.dims, t.dims + t.ndims },
                          std::string(t.ptr, t.size) });
            }
        }
        EXPECT_EQ(covered, file.size()) << "seed " << seed;
        EXPECT_EQ(lexed, model.expected) << "seed " << seed;
    }
}

} // namespace
} // namespace openzl::custom_parsers
