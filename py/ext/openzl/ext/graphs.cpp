// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/ext/graphs.hpp" // @manual

#include <any>
#include <memory>

#include <zstd.h>
#include "openzl/ext.hpp"
#include "openzl/ext/utils.hpp"
#include "openzl/openzl.hpp"

#include "tools/sddl/compiler/Compiler.h" // @manual

namespace openzl {
namespace py {
namespace {

template <typename T>
std::string graphDocstring()
{
    const auto& meta = T::metadata;
    std::string docs = meta.description;
    docs += "\n\nInputs:\n";
    docs += ioDocs(meta.inputs, true);
    if (meta.lastInputIsVariable) {
        docs += "\t...";
    }
    return docs;
}

class PyGraphTrampoline : public PyGraphBase {
   public:
    NB_TRAMPOLINE(PyGraphBase, 5);

    GraphID parameterize(nb::ref<PyCompressor> compressor) const override
    {
        NB_OVERRIDE(parameterize, compressor);
    }

    void setDestination(nb::ref<PyEdge> edge) const override
    {
        NB_OVERRIDE_NAME("set_destination", setDestination, edge);
    }

    void setMultiInputDestination(
            std::vector<nb::ref<PyEdge>> edges) const override
    {
        NB_OVERRIDE_NAME(
                "set_multi_input_destination", setMultiInputDestination, edges);
    }

    GraphID baseGraph() const override
    {
        NB_OVERRIDE_PURE_NAME("base_graph", baseGraph);
    }

    poly::optional<GraphParameters> parameters() const override
    {
        NB_OVERRIDE(parameters);
    }

    ~PyGraphTrampoline() override = default;
};

template <typename GraphT>
class PyGraph : public PyGraphBase {
   public:
    template <typename... Args>
    explicit PyGraph(Args&&... args) : graph_(std::forward<Args>(args)...)
    {
    }

    PyGraph(const PyGraph&) = default;
    PyGraph(PyGraph&&)      = default;

    PyGraph& operator=(const PyGraph&) = default;
    PyGraph& operator=(PyGraph&&)      = default;

    GraphID parameterize(nb::ref<PyCompressor> compressor) const override
    {
        return graph_.parameterize(*compressor);
    }

    void setDestination(nb::ref<PyEdge> edge) const override
    {
        graph_.setDestination(*edge->get());
    }

    void setMultiInputDestination(
            std::vector<nb::ref<PyEdge>> edges) const override
    {
        auto inEdges = PyEdge::convert(edges);
        return graph_.setMultiInputDestination(inEdges);
    }

    GraphID baseGraph() const override
    {
        return graph_.baseGraph();
    }

    poly::optional<GraphParameters> parameters() const override
    {
        return graph_.parameters();
    }

    /// Preserve the lifetime of @p val
    template <typename T>
    void stash(T val)
    {
        stash_.emplace_back(std::move(val));
    }

    ~PyGraph() override = default;

   private:
    GraphT graph_;
    std::vector<std::any> stash_;
};

void registerGraphBaseClass(nb::module_& g)
{
    nb::class_<PyGraphBase, PyGraphTrampoline>(
            g,
            "Graph",
            nb::intrusive_ptr<PyGraphBase>(
                    [](PyGraphBase* o, PyObject* po) noexcept {
                        o->set_self_py(po);
                    }))
            .def(nb::init<>())
            .def("base_graph", &PyGraphBase::baseGraph)
            .def("parameters", &PyGraphBase::parameters)
            .def("set_destination", &PyGraphBase::setDestination)
            .def("set_multi_input_destination",
                 &PyGraphBase::setMultiInputDestination)
            .def("parameterize", &PyGraphBase::parameterize);
}

template <typename GraphT>
nb::class_<PyGraph<GraphT>, PyGraphBase> registerGraph(
        nb::module_& g,
        const char* name)
{
    using G   = PyGraph<GraphT>;
    auto docs = graphDocstring<GraphT>();
    // TODO(terrelln): Expose parameters
    return nb::class_<G, PyGraphBase>(g, name, docs.c_str())
            .def("__call__", &G::parameterize)
            .def("parameterize", &G::parameterize, nb::arg("compressor"))
            .def("set_destination", &G::setDestination, nb::arg("edge"))
            .def("set_multi_input_destination",
                 &G::setMultiInputDestination,
                 nb::arg("edges"))
            .def_prop_ro("base_graph", &G::baseGraph);
}

template <typename GraphT>
nb::class_<PyGraph<GraphT>, PyGraphBase> registerSimpleGraph(
        nb::module_& g,
        const char* name)
{
    return registerGraph<GraphT>(g, name).def(nb::init<>());
}

void registerBitpackGraph(nb::module_& g)
{
    registerSimpleGraph<graphs::Bitpack>(g, "Bitpack");
}

void registerBruteForceGraph(nb::module_& g)
{
    // TODO(terrelln): Hook it up once C++ is ready
}

void registerCompressGraph(nb::module_& g)
{
    registerSimpleGraph<graphs::Compress>(g, "Compress");
}

void registerConstantGraph(nb::module_& g)
{
    registerSimpleGraph<graphs::Constant>(g, "Constant");
}

void registerEntropyGraphs(nb::module_& g)
{
    registerSimpleGraph<graphs::Entropy>(g, "Entropy");
    registerSimpleGraph<graphs::Huffman>(g, "Huffman");
    registerSimpleGraph<graphs::Fse>(g, "Fse");
}

void registerFieldLzGraphs(nb::module_& g)
{
    registerGraph<graphs::FieldLz>(g, "FieldLz")
            .def(
                    "__init__",
                    [](PyGraph<graphs::FieldLz>* obj,
                       poly::optional<int> compressionLevel,
                       poly::optional<GraphID> literalsGraph,
                       poly::optional<GraphID> tokensGraph,
                       poly::optional<GraphID> offsetsGraph,
                       poly::optional<GraphID> extraLiteralLengthsGraph,
                       poly::optional<GraphID> extraMatchLengthsGraph) {
                        if (!compressionLevel.has_value()
                            && !literalsGraph.has_value()
                            && !tokensGraph.has_value()
                            && !offsetsGraph.has_value()
                            && !extraLiteralLengthsGraph.has_value()
                            && !extraMatchLengthsGraph.has_value()) {
                            new (obj)
                                    PyGraph<graphs::FieldLz>(graphs::FieldLz{});

                        } else {
                            new (obj) PyGraph<graphs::FieldLz>(
                                    graphs::FieldLz(graphs::FieldLz::Parameters{
                                            .compressionLevel =
                                                    std::move(compressionLevel),
                                            .literalsGraph =
                                                    std::move(literalsGraph),
                                            .tokensGraph =
                                                    std::move(tokensGraph),
                                            .offsetsGraph =
                                                    std::move(offsetsGraph),
                                            .extraLiteralLengthsGraph = std::move(
                                                    extraLiteralLengthsGraph),
                                            .extraMatchLengthsGraph = std::move(
                                                    extraMatchLengthsGraph),
                                    }));
                        }
                    },
                    nb::kw_only(),
                    nb::arg("compression_level")          = poly::nullopt,
                    nb::arg("literals_graph")             = poly::nullopt,
                    nb::arg("tokens_graph")               = poly::nullopt,
                    nb::arg("offsets_graph")              = poly::nullopt,
                    nb::arg("extr_literal_lengths_graph") = poly::nullopt,
                    nb::arg("extra_match_lengths_graph")  = poly::nullopt);
}

void registerFlatpackGraph(nb::module_& g)
{
    registerSimpleGraph<graphs::Flatpack>(g, "Flatpack");
}

void registerMergeSortedGraph(nb::module_& g)
{
    // TODO(terrelln): Hook up once C++ supports it
}

void registerSDDLGraph(nb::module_& g)
{
    registerGraph<graphs::SDDL>(g, "SDDL").def(
            "__init__",
            [](PyGraph<graphs::SDDL>* obj,
               std::string description,
               GraphID successor) {
                std::stringstream logs;
                auto compiled = std::make_shared<std::string>();
                try {
                    *compiled =
                            sddl::Compiler{ sddl::Compiler::Options{}.with_log(
                                                    logs) }
                                    .compile(description, "[local_input]");
                } catch (const sddl::CompilerException&) {
                    // To-Do: allow adding the error logs somehow?
                    (void)logs;
                    throw;
                }
                new (obj) PyGraph<graphs::SDDL>(*compiled, successor);
                obj->stash(std::move(compiled));
            },
            nb::kw_only(),
            nb::arg("description"),
            nb::arg("successor"));
}

void registerStoreGraph(nb::module_& g)
{
    registerSimpleGraph<graphs::Store>(g, "Store");
}

void registerZstdGraph(nb::module_& g)
{
    registerGraph<graphs::Zstd>(g, "Zstd").def(
            "__init__",
            [](PyGraph<graphs::Zstd>* obj,
               poly::optional<int> compressionLevel,
               poly::optional<std::unordered_map<int, int>> zstdParams) {
                if (compressionLevel.has_value() && zstdParams.has_value()) {
                    zstdParams->insert_or_assign(
                            ZSTD_c_compressionLevel, *compressionLevel);
                }
                if (zstdParams.has_value()) {
                    new (obj) PyGraph<graphs::Zstd>(
                            graphs::Zstd(std::move(*zstdParams)));
                } else if (compressionLevel.has_value()) {
                    new (obj) PyGraph<graphs::Zstd>(
                            graphs::Zstd(std::move(*compressionLevel)));
                } else {
                    new (obj) PyGraph<graphs::Zstd>(graphs::Zstd());
                }
            },
            nb::kw_only(),
            nb::arg("compression_level") = poly::nullopt,
            nb::arg("zstd_params")       = poly::nullopt);
}
} // namespace

void registerGraphsModule(nb::module_& m)
{
    auto g = m.def_submodule("graphs");
    registerGraphBaseClass(g);
    registerBitpackGraph(g);
    registerBruteForceGraph(g);
    registerCompressGraph(g);
    registerConstantGraph(g);
    registerEntropyGraphs(g);
    registerFieldLzGraphs(g);
    registerFlatpackGraph(g);
    registerMergeSortedGraph(g);
    registerSDDLGraph(g);
    registerStoreGraph(g);
    registerZstdGraph(g);
}

} // namespace py
} // namespace openzl
