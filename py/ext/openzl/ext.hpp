// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <string>

#include <nanobind/nanobind.h>

#include <nanobind/intrusive/counter.h>
#include <nanobind/intrusive/ref.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>
#include <nanobind/trampoline.h>

#include "openzl/openzl.hpp"

namespace openzl {
namespace py {
namespace nb = nanobind;

template <typename T>
class Traversable {
   public:
    std::vector<nb::handle> references() const
    {
        throw Exception("Must be implemented!");
    }

    void clear()
    {
        auto obj = static_cast<T*>(this);
        obj->~T();
        new (obj) T();
    }

   private:
    static int tp_traverse(PyObject* self, visitproc visit, void* arg)
    {
        // On Python 3.9+, we must traverse the implicit dependency
        // of an object on its associated type object.
        if (PY_VERSION_HEX >= 0x03090000) {
            Py_VISIT(Py_TYPE(self));
        }

        // The tp_traverse method may be called after __new__ but before or
        // during __init__, before the C++ constructor has been completed. We
        // must not inspect the C++ state if the constructor has not yet
        // completed.
        if (!nb::inst_ready(self)) {
            return 0;
        }

        // Get the C++ object associated with 'self' (this always succeeds)
        auto compressor = nb::inst_ptr<T>(self);
        for (auto& ref : compressor->references()) {
            Py_VISIT(ref.ptr());
        }
        return 0;
    }

    static int tp_clear(PyObject* self)
    {
        // Get the C++ object associated with 'self' (this always succeeds)
        auto obj = nb::inst_ptr<T>(self);

        // Break the reference cycle
        obj->clear();

        return 0;
    }

   public:
    static const std::array<PyType_Slot, 3> typeSlots;
};

template <typename T>
const std::array<PyType_Slot, 3> Traversable<T>::typeSlots = {
    { { Py_tp_traverse, (void*)(tp_traverse) },
      { Py_tp_clear, (void*)(tp_clear) },
      { 0, nullptr } }
};

class PyInput : public Input, public nb::intrusive_base {
   public:
    explicit PyInput(Input&& input) : Input(std::move(input)) {}
};

std::vector<nb::ref<const PyInput>> toPyInputs(
        poly::span<const InputRef> inputs);

nb::ref<const PyInput> toPyInput(const Input& input);

class PyOutput : public Output, public nb::intrusive_base {
   public:
    explicit PyOutput(Output&& output) : Output(std::move(output)) {}

    void reserveStringLens(size_t numElts)
    {
        mutStringLens_ = static_cast<Output*>(this)->reserveStringLens(numElts);
    }

    uint32_t* stringLens()
    {
        if (type() != Type::String) {
            throw Exception(
                    "Output: Cannot get string lens for non-string type");
        }
        if (!mutStringLens_) {
            throw Exception(
                    "Output: Must call reserve_string_lens() before getting mutable output buffers for string types");
        }
        return mutStringLens_;
    }
    const uint32_t* stringLens() const
    {
        return static_cast<const Output*>(this)->stringLens();
    }

    void commit(size_t numElts)
    {
        static_cast<Output*>(this)->commit(numElts);
        committed_ = true;
    }

    bool committed() const
    {
        return committed_;
    }

   private:
    bool committed_{ false };
    uint32_t* mutStringLens_{ nullptr };
};

class PyEncoderState : public nb::intrusive_base {
   public:
    static nb::ref<PyEncoderState> create(EncoderState& state)
    {
        return new PyEncoderState(state);
    }

    std::vector<nb::ref<const PyInput>> inputs() const
    {
        return inputs_;
    }

    nb::ref<PyOutput>
    createOutput(size_t idx, size_t maxNumElts, size_t eltWidth)
    {
        auto out = state_->createOutput(idx, maxNumElts, eltWidth);
        return new PyOutput(std::move(out));
    }

    int getCParam(CParam param) const
    {
        return state_->getCParam(param);
    }

    poly::optional<int> getLocalIntParam(int key) const
    {
        return state_->getLocalIntParam(key);
    }

    poly::optional<nb::bytes> getLocalParam(int key) const
    {
        auto data = state_->getLocalParam(key);
        if (!data.has_value()) {
            return poly::nullopt;
        }
        return nb::bytes(data->data(), data->size());
    }

    void sendCodecHeader(const nb::bytes& data)
    {
        state_->sendCodecHeader(data.data(), data.size());
    }

   private:
    explicit PyEncoderState(EncoderState& state)
            : state_(&state), inputs_(toPyInputs(state.inputs()))
    {
    }
    EncoderState* state_;
    std::vector<nb::ref<const PyInput>> inputs_;
};

class PyCustomEncoder : public CustomEncoder, public nb::intrusive_base {
   public:
    using CustomEncoder::CustomEncoder;

    virtual void encode(nb::ref<PyEncoderState> state) const = 0;

    void encode(EncoderState& state) const override
    {
        auto pyState = PyEncoderState::create(state);
        return encode(std::move(pyState));
    }
};

class PyCustomEncoderTrampoline : public PyCustomEncoder {
   public:
    NB_TRAMPOLINE(PyCustomEncoder, 2);

    MultiInputCodecDescription multiInputDescription() const override
    {
        NB_OVERRIDE_NAME("multi_input_description", multiInputDescription);
    }

    void encode(nb::ref<PyEncoderState> state) const override
    {
        NB_OVERRIDE_PURE(encode, state);
    }
};

class PyEdge : public nb::intrusive_base {
   public:
    Edge* get()
    {
        return &edge_;
    }

    Edge& operator*()
    {
        return *get();
    }

    static std::vector<nb::ref<PyEdge>> convert(poly::span<Edge> edges)
    {
        std::vector<nb::ref<PyEdge>> result;
        result.reserve(edges.size());
        for (auto& edge : edges) {
            result.push_back(PyEdge::create(edge));
        }
        return result;
    }

    static std::vector<nb::ref<PyEdge>> convert(std::vector<Edge>&& edges)
    {
        return convert(poly::span<Edge>(edges));
    }

    static std::vector<Edge> convert(poly::span<nb::ref<PyEdge>> edges)
    {
        std::vector<Edge> result;
        result.reserve(edges.size());
        for (auto& edge : edges) {
            result.emplace_back((**edge).get());
        }
        return result;
    }

    static nb::ref<PyEdge> create(Edge& edge)
    {
        return new PyEdge(edge);
    }

    static nb::ref<PyEdge> create(Edge&& edge)
    {
        return new PyEdge(edge);
    }

    nb::ref<const PyInput> getInput() const
    {
        return input_;
    }

    std::vector<nb::ref<PyEdge>> runNode(
            NodeID node,
            poly::optional<std::string> name,
            poly::optional<LocalParams> localParams)
    {
        auto edges = edge_.runNode(
                node,
                NodeParameters{ .name        = std::move(name),
                                .localParams = std::move(localParams) });
        return PyEdge::convert(edges);
    }

    static std::vector<nb::ref<PyEdge>> runMultiInputNode(
            std::vector<nb::ref<PyEdge>> inputs,
            NodeID node,
            poly::optional<std::string> name,
            poly::optional<LocalParams> localParams)
    {
        auto inEdges  = PyEdge::convert(inputs);
        auto outEdges = Edge::runMultiInputNode(
                inEdges,
                node,
                NodeParameters{
                        .name        = std::move(name),
                        .localParams = std::move(localParams),
                });
        return PyEdge::convert(outEdges);
    }

    void setIntMetadata(int key, int value)
    {
        edge_.setIntMetadata(key, value);
    }

    void setDestination(
            GraphID graph,
            poly::optional<std::string> name,
            poly::optional<std::vector<GraphID>> customGraphs,
            poly::optional<std::vector<NodeID>> customNodes,
            poly::optional<LocalParams> localParams)
    {
        edge_.setDestination(
                graph,
                GraphParameters{ .name         = std::move(name),
                                 .customGraphs = std::move(customGraphs),
                                 .customNodes  = std::move(customNodes),
                                 .localParams  = std::move(localParams) });
    }
    static void setMultiInputDestination(
            std::vector<nb::ref<PyEdge>> inputs,
            GraphID graph,
            poly::optional<std::string> name,
            poly::optional<std::vector<GraphID>> customGraphs,
            poly::optional<std::vector<NodeID>> customNodes,
            poly::optional<LocalParams> localParams)
    {
        auto edges = PyEdge::convert(inputs);
        Edge::setMultiInputDestination(
                edges,
                graph,
                GraphParameters{ .name         = std::move(name),
                                 .customGraphs = std::move(customGraphs),
                                 .customNodes  = std::move(customNodes),
                                 .localParams  = std::move(localParams) });
    }

   private:
    explicit PyEdge(Edge& edge)
            : edge_(edge.get()), input_(toPyInput(edge_.getInput()))
    {
    }

    Edge edge_;
    nb::ref<const PyInput> input_;
};

class PyGraphState : public nb::intrusive_base {
   public:
    static nb::ref<PyGraphState> create(GraphState& state)
    {
        return new PyGraphState(state);
    }

    std::vector<nb::ref<PyEdge>> edges()
    {
        return edges_;
    }

    std::vector<GraphID> customGraphs() const
    {
        auto graphs = state_->customGraphs();
        return { graphs.begin(), graphs.end() };
    }

    std::vector<NodeID> customNodes() const
    {
        auto nodes = state_->customNodes();
        return { nodes.begin(), nodes.end() };
    }

    int getCParam(CParam param) const
    {
        return state_->getCParam(param);
    }

    poly::optional<int> getLocalIntParam(int key) const
    {
        return state_->getLocalIntParam(key);
    }

    poly::optional<nb::bytes> getLocalParam(int key) const
    {
        auto param = state_->getLocalParam(key);
        if (param.has_value()) {
            return nb::bytes(param->data(), param->size());
        } else {
            return poly::nullopt;
        }
    }

    bool isNodeSupported(NodeID node) const
    {
        return state_->isNodeSupported(node);
    }

   private:
    explicit PyGraphState(GraphState& state)
            : state_(&state), edges_(PyEdge::convert(state.edges()))
    {
    }

    GraphState* state_;
    std::vector<nb::ref<PyEdge>> edges_;
};

class PyFunctionGraph : public FunctionGraph, public nb::intrusive_base {
   public:
    using FunctionGraph::FunctionGraph;

    virtual void graph(nb::ref<PyGraphState> state) const = 0;

    void graph(GraphState& state) const override
    {
        auto pyState = PyGraphState::create(state);
        graph(std::move(pyState));
    }

   private:
};

class PyFunctionGraphTrampoline : public PyFunctionGraph {
   public:
    NB_TRAMPOLINE(PyFunctionGraph, 2);

    FunctionGraphDescription functionGraphDescription() const override
    {
        NB_OVERRIDE_PURE_NAME(
                "function_graph_description", functionGraphDescription);
    }

    void graph(nb::ref<PyGraphState> state) const override
    {
        NB_OVERRIDE_PURE(graph, state);
    }
};

class PySelectorState : public nb::intrusive_base {
   public:
    static nb::ref<PySelectorState> create(SelectorState& state)
    {
        return new PySelectorState(state);
    }

    std::vector<GraphID> customGraphs() const
    {
        auto graphs = state_->customGraphs();
        return { graphs.begin(), graphs.end() };
    }

    int getCParam(CParam param) const
    {
        return state_->getCParam(param);
    }

    poly::optional<int> getLocalIntParam(int key) const
    {
        return state_->getLocalIntParam(key);
    }

    poly::optional<nb::bytes> getLocalParam(int key) const
    {
        auto param = state_->getLocalParam(key);
        if (param.has_value()) {
            return nb::bytes(param->data(), param->size());
        } else {
            return poly::nullopt;
        }
    }

    void parameterizeDestination(
            poly::optional<std::string> name,
            poly::optional<std::vector<GraphID>> customGraphs,
            poly::optional<std::vector<NodeID>> customNodes,
            poly::optional<LocalParams> localParams)
    {
        state_->parameterizeDestination(
                GraphParameters{
                        .name         = std::move(name),
                        .customGraphs = std::move(customGraphs),
                        .customNodes  = std::move(customNodes),
                        .localParams  = std::move(localParams),
                });
    }

   private:
    explicit PySelectorState(SelectorState& state) : state_(&state) {}

    SelectorState* state_;
};

class PySelector : public Selector, public nb::intrusive_base {
   public:
    using Selector::Selector;

    virtual GraphID select(nb::ref<PySelectorState> state, const PyInput& input)
            const = 0;

    GraphID select(SelectorState& state, const Input& input) const override
    {
        auto pyState = PySelectorState::create(state);
        return select(std::move(pyState), *toPyInput(input));
    }
};

class PySelectorTrampoline : public PySelector {
   public:
    NB_TRAMPOLINE(PySelector, 2);

    SelectorDescription selectorDescription() const override
    {
        NB_OVERRIDE_PURE_NAME("selector_description", selectorDescription);
    }

    GraphID select(nb::ref<PySelectorState> state, const PyInput& input)
            const override
    {
        NB_OVERRIDE_PURE(select, state, input);
    }
};

class PyCompressor : public Compressor,
                     public Traversable<PyCompressor>,
                     public nb::intrusive_base {
   public:
    using Compressor::Compressor;

    GraphID buildStaticGraph(
            NodeID headNode,
            std::vector<GraphID> successorGraphs,
            poly::optional<std::string> name,
            poly::optional<LocalParams> localParams)
    {
        return this->Compressor::buildStaticGraph(
                headNode,
                successorGraphs,
                StaticGraphParameters{
                        .name        = std::move(name),
                        .localParams = std::move(localParams),
                });
    }

    NodeID parameterizeNode(
            NodeID node,
            poly::optional<std::string> name,
            poly::optional<LocalParams> localParams)
    {
        return this->Compressor::parameterizeNode(
                node,
                {
                        .name        = std::move(name),
                        .localParams = std::move(localParams),
                });
    }

    GraphID parameterizeGraph(
            GraphID graph,
            poly::optional<std::string> name,
            poly::optional<std::vector<GraphID>> customGraphs,
            poly::optional<std::vector<NodeID>> customNodes,
            poly::optional<LocalParams> localParams)
    {
        return this->Compressor::parameterizeGraph(
                graph,
                {
                        .name         = std::move(name),
                        .customGraphs = std::move(customGraphs),
                        .customNodes  = std::move(customNodes),
                        .localParams  = std::move(localParams),
                });
    }

    NodeID registerCustomEncoder(nb::ref<PyCustomEncoder> encoder)
    {
        reference(*encoder);
        auto raw           = encoder.get();
        auto sharedEncoder = std::shared_ptr<CustomEncoder>(
                raw, [encoder = std::move(encoder)](CustomEncoder* ptr) {});
        return this->Compressor::registerCustomEncoder(
                std::move(sharedEncoder));
    }

    GraphID registerFunctionGraph(nb::ref<PyFunctionGraph> graph)
    {
        reference(*graph);
        auto raw         = graph.get();
        auto sharedGraph = std::shared_ptr<FunctionGraph>(
                raw, [graph = std::move(graph)](FunctionGraph*) {});
        return this->Compressor::registerFunctionGraph(std::move(sharedGraph));
    }

    GraphID registerSelectorGraph(nb::ref<PySelector> graph)
    {
        reference(*graph);
        auto raw         = graph.get();
        auto sharedGraph = std::shared_ptr<Selector>(
                raw, [graph = std::move(graph)](Selector*) {});
        return this->Compressor::registerSelectorGraph(std::move(sharedGraph));
    }

    nb::bytes serialize() const
    {
        auto data = this->Compressor::serialize();
        return nb::bytes(data.data(), data.size());
    }

    void deserialize(const nb::bytes& data)
    {
        this->Compressor::deserialize(
                { (const char*)data.data(), data.size() });
    }

    UnmetDependencies getUnmetDependencies(const nb::bytes& data) const
    {
        return this->Compressor::getUnmetDependencies(
                { (const char*)data.data(), data.size() });
    }

    std::vector<nb::handle> references() const
    {
        return references_;
    }

   private:
    template <typename T>
    void reference(const T& val)
    {
        nb::handle obj = nb::find(val);
        if (obj.ptr() != nullptr) {
            references_.push_back(std::move(obj));
        }
    }

    std::vector<nb::handle> references_;
};

} // namespace py
} // namespace openzl
