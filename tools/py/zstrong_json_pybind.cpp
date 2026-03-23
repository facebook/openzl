// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <folly/FileUtil.h>
#include <folly/json.h>

#include "openzl/common/scope_context.h"
#include "openzl/common/stream.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_selector.h"
#include "tools/py/pybind_helpers.h"
#include "tools/py/zstrong_ml_pybind.h"
#include "tools/zstrong_json.h"

namespace zstrong {
namespace {
namespace py = pybind11;

std::string typeName(ZL_Type type)
{
    std::string name;
    auto addType = [&name](std::string_view typeStr) {
        if (!name.empty()) {
            name += " | ";
        }
        name += typeStr;
    };

    if (type & ZL_Type_serial) {
        addType("serial");
    }
    if (type & ZL_Type_numeric) {
        addType("numeric");
    }
    if (type & ZL_Type_struct) {
        addType("fixed_size_field");
    }
    if (type & ZL_Type_string) {
        addType("variable_size_field");
    }
    return name;
}

template <typename Ctx, typename CreateStreamFn>
ZL_Report fillFromObject(
        Ctx* cctx,
        CreateStreamFn&& createStream,
        int idx,
        ZL_Type streamType,
        py::handle const& handle)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    try {
        if (streamType == ZL_Type_string) {
            auto const list   = handle.cast<py::list>();
            ZL_Output* stream = pybind::listToStream(
                    list, streamType, [idx, createStream](size_t contentSize) {
                        return createStream(idx, contentSize, 1);
                    });
            ZL_ERR_IF_NULL(stream, allocation);
        } else {
            auto const buffer = handle.cast<py::buffer>();
            auto const info   = buffer.request();
            ZL_Output* stream = pybind::bufferToStream(
                    info,
                    streamType,
                    [idx, createStream](size_t nbElts, size_t eltWidth) {
                        return createStream(idx, nbElts, eltWidth);
                    });
            ZL_ERR_IF_NULL(stream, allocation);
        }
        return ZL_returnSuccess();
    } catch (py::cast_error const& e) {
        ZL_ERR(transform_executionFailure,
               "Stream returned by python fn %d is not the right type: %s!",
               idx,
               e.what());
    } catch (std::exception const& e) {
        ZL_ERR(transform_executionFailure,
               "Unexpected error reading stream %d: %s",
               idx,
               e.what());
    }
}

template <typename EICtx>
class PyEncoderCtxImpl {
   public:
    explicit PyEncoderCtxImpl(EICtx* eictx) : eictx_(eictx) {}

    int getGlobalParam(ZL_CParam param) const
    {
        return ZL_Encoder_getCParam(eictx_, param);
    }
    std::optional<int> getLocalIntParam(int paramID) const
    {
        auto const ret = ZL_Encoder_getLocalIntParam(eictx_, paramID);
        if (ret.paramId == ZL_LP_INVALID_PARAMID) {
            return std::nullopt;
        }
        return ret.paramValue;
    }
    std::optional<py::bytes> getLocalBinaryParam(int paramID) const
    {
        auto const ret = ZL_Encoder_getLocalCopyParam(eictx_, paramID);
        if (ret.paramId == ZL_LP_INVALID_PARAMID) {
            return std::nullopt;
        }
        return py::bytes((char const*)ret.paramPtr, ret.paramSize);
    }
    std::optional<std::string> getLocalStringParam(int paramID) const
    {
        auto const ret = ZL_Encoder_getLocalCopyParam(eictx_, paramID);
        if (ret.paramId == ZL_LP_INVALID_PARAMID) {
            return std::nullopt;
        }
        return std::string((char const*)ret.paramPtr, ret.paramSize);
    }

   protected:
    EICtx* eictx_;
};

class PySelectorCtx {
   public:
    PySelectorCtx(ZL_Selector const* selCtx, ZL_Type inputType)
            : selCtx_(selCtx), inputType_(inputType)
    {
    }

    std::optional<size_t> tryGraph(py::object const& input, ZL_GraphID graph)
            const
    {
        std::optional<size_t> result;
        ZL_Data /* terrelln-codemod-skip */* stream =
                STREAM_create(ZL_DATA_ID_INPUTSTREAM);
        if (stream == NULL)
            return std::nullopt;

        auto createStream =
                [stream, this](
                        int, size_t nbElts, size_t eltWidth) -> ZL_Output* {
            if (ZL_isError(
                        STREAM_reserve(stream, inputType_, eltWidth, nbElts)))
                return nullptr;
            return ZL_codemodDataAsOutput(stream);
        };
        if (!ZL_isError(fillFromObject(
                    selCtx_, createStream, 0, inputType_, input))) {
            auto const ret = ZL_Selector_tryGraph(
                    selCtx_, ZL_codemodDataAsInput(stream), graph);
            if (!ZL_isError(ret.finalCompressedSize)) {
                result = ZL_validResult(ret.finalCompressedSize);
            }
        }
        STREAM_free(stream);
        return result;
    }

    int getGlobalParam(ZL_CParam param) const
    {
        return ZL_Selector_getCParam(selCtx_, param);
    }
    std::optional<int> getLocalIntParam(int paramID) const
    {
        auto const ret = ZL_Selector_getLocalIntParam(selCtx_, paramID);
        if (ret.paramId == ZL_LP_INVALID_PARAMID) {
            return std::nullopt;
        }
        return ret.paramValue;
    }
    std::optional<py::bytes> getLocalBinaryParam(int paramID) const
    {
        auto const ret = ZL_Selector_getLocalCopyParam(selCtx_, paramID);
        if (ret.paramId == ZL_LP_INVALID_PARAMID) {
            return std::nullopt;
        }
        return py::bytes((char const*)ret.paramPtr, ret.paramSize);
    }
    std::optional<std::string> getLocalStringParam(int paramID) const
    {
        auto const ret = ZL_Selector_getLocalCopyParam(selCtx_, paramID);
        if (ret.paramId == ZL_LP_INVALID_PARAMID) {
            return std::nullopt;
        }
        return std::string((char const*)ret.paramPtr, ret.paramSize);
    }

   private:
    const ZL_Selector* selCtx_;
    ZL_Type inputType_;
};

class PyStream {
   public:
    explicit PyStream(ZL_Input const* stream) : stream_(stream) {}

    ZL_Type type() const
    {
        return ZL_Input_type(stream_);
    }

    py::array asArray() const
    {
        return pybind::toNumpyArray(stream_);
    }

    py::bytes asBytes() const
    {
        if (ZL_Input_type(stream_) != ZL_Type_serial) {
            throw std::runtime_error{ "Only serialized streams supported!" };
        }
        return py::bytes(
                (const char*)ZL_Input_ptr(stream_), ZL_Input_numElts(stream_));
    }

    py::list asList() const
    {
        return pybind::toList(stream_);
    }

   private:
    ZL_Input const* stream_;
};

class PySimpleEncoderCtx : public PyEncoderCtxImpl<ZL_Encoder> {
   public:
    using PyEncoderCtxImpl<ZL_Encoder>::PyEncoderCtxImpl;

    void sendTransformHeader(py::bytes header)
    {
        auto const view = header.cast<std::string_view>();
        ZL_Encoder_sendCodecHeader(eictx_, view.data(), view.size());
    }
};

class PySimpleDecoderCtx {
   public:
    explicit PySimpleDecoderCtx(ZL_Decoder const* dictx) : dictx_(dictx) {}

    py::bytes getTransformHeader() const
    {
        auto buffer = ZL_Decoder_getCodecHeader(dictx_);
        if (buffer.size == 0)
            return py::bytes();
        return py::bytes((const char*)buffer.start, buffer.size);
    }

   private:
    ZL_Decoder const* dictx_;
};

class PyEncoderCtx : public PyEncoderCtxImpl<ZL_Encoder> {
   public:
    PyEncoderCtx(
            ZL_Encoder* eictx,
            Transform const* transform,
            ZL_Input const* inputs[],
            size_t nbInputs,
            ZL_Report* report)
            : PyEncoderCtxImpl(eictx),
              transform_(transform),
              inputs_(inputs),
              nbInputs_(nbInputs),
              report_(report)
    {
        *report_ = ZL_returnSuccess();
    }

    PySimpleEncoderCtx asSimpleEncoderCtx() const
    {
        return PySimpleEncoderCtx(eictx_);
    }

    void sendTransformHeader(py::bytes header)
    {
        auto const view = header.cast<std::string_view>();
        ZL_Encoder_sendCodecHeader(eictx_, view.data(), view.size());
    }

    py::tuple getInputs() const
    {
        return toPyInputs(inputs_, nbInputs_);
    }

    PyStream getInput(size_t idx) const
    {
        return PyStream(inputs_[idx]);
    }

    void createOutput(size_t idx, py::object stream)
    {
        if (ZL_isError(*report_)) {
            return;
        }
        auto createStream = [eictx = eictx_](
                                    int idx, size_t nbElts, size_t eltWidth) {
            return ZL_Encoder_createTypedStream(eictx, idx, nbElts, eltWidth);
        };
        *report_ = fillFromObject(
                eictx,
                createStream,
                (int)idx,
                transform_->outputType(idx),
                stream);
    }

   private:
    static py::tuple toPyInputs(ZL_Input const** inputs, size_t nbInputs)
    {
        py::list pyInputs;
        for (size_t i = 0; i < nbInputs; ++i) {
            pyInputs.append(PyStream(inputs[i]));
        }
        return pyInputs.cast<py::tuple>();
    }

    Transform const* transform_;
    ZL_Input const** inputs_;
    size_t nbInputs_;
    ZL_Report* report_;
};

class PyDecoderCtx {
   public:
    explicit PyDecoderCtx(
            ZL_Decoder* dictx,
            Transform const* transform,
            ZL_Input const* fixedInputs[],
            size_t nbFixedInputs,
            ZL_Input const* voInputs[],
            size_t nbVoInputs,
            ZL_Report* report)
            : dictx_(dictx),
              transform_(transform),
              fixedInputs_(fixedInputs),
              nbFixedInputs_(nbFixedInputs),
              voInputs_(voInputs),
              nbVoInputs_(nbVoInputs),
              report_(report)
    {
        *report_ = ZL_returnSuccess();
    }

    PySimpleDecoderCtx asSimpleDecoderCtx() const
    {
        return PySimpleDecoderCtx(dictx_);
    }

    py::bytes getTransformHeader() const
    {
        auto buffer = ZL_Decoder_getCodecHeader(dictx_);
        if (buffer.size == 0)
            return py::bytes();
        return py::bytes((const char*)buffer.start, buffer.size);
    }

    py::tuple getFixedInputs() const
    {
        return toPyInputs(fixedInputs_, nbFixedInputs_);
    }

    py::tuple getVariableInputs() const
    {
        return toPyInputs(voInputs_, nbVoInputs_);
    }

    void createOutput(size_t idx, py::object stream)
    {
        if (ZL_isError(*report_)) {
            return;
        }
        auto createStream = [dictx = dictx_](
                                    int idx, size_t nbElts, size_t eltWidth) {
            return ZL_Decoder_createTypedStream(dictx, idx, nbElts, eltWidth);
        };
        *report_ = fillFromObject(
                eictx, createStream, idx, transform_->inputType(idx), stream);
    }

   private:
    static py::tuple toPyInputs(ZL_Input const** inputs, size_t nbInputs)
    {
        py::list pyInputs;
        for (size_t i = 0; i < nbInputs; ++i) {
            pyInputs.append(PyStream(inputs[i]));
        }
        return pyInputs.cast<py::tuple>();
    }

    ZL_Decoder* dictx_;
    Transform const* transform_;
    ZL_Input const** fixedInputs_;
    size_t nbFixedInputs_;
    ZL_Input const** voInputs_;
    size_t nbVoInputs_;
    ZL_Report* report_;
};
;

class PyCustomTransform {
   public:
    PyCustomTransform(
            ZL_IDType id_,
            std::vector<ZL_Type> inputTypes_,
            std::vector<ZL_Type> fixedOutputTypes_,
            std::vector<ZL_Type> variableOutputTypes_,
            std::string docs_)
            : id(id_),
              inputTypes(std::move(inputTypes_)),
              fixedOutputTypes(std::move(fixedOutputTypes_)),
              variableOutputTypes(std::move(variableOutputTypes_)),
              docs(std::move(docs_))
    {
    }

    ZL_IDType const id;
    std::vector<ZL_Type> const inputTypes;
    std::vector<ZL_Type> const fixedOutputTypes;
    std::vector<ZL_Type> const variableOutputTypes;
    std::string const docs;

    virtual void encode(PyEncoderCtx ctx) const = 0;

    virtual void decode(PyDecoderCtx ctx) const = 0;

    virtual ~PyCustomTransform() {}
};

class PyCustomTransformTrampoline : public PyCustomTransform {
   public:
    using PyCustomTransform::PyCustomTransform;

    void encode(PyEncoderCtx ctx) const override
    {
        PYBIND11_OVERRIDE_PURE(void, PyCustomTransform, encode, ctx);
    }

    void decode(PyDecoderCtx ctx) const override
    {
        PYBIND11_OVERRIDE_PURE(void, PyCustomTransform, decode, ctx);
    }
};

class PySimpleCustomTransform : public PyCustomTransform {
   public:
    PySimpleCustomTransform(
            ZL_IDType id_,
            ZL_Type inputType_,
            std::vector<ZL_Type> outputTypes_,
            std::string docs_)
            : PyCustomTransform(
                      id_,
                      { inputType_ },
                      std::move(outputTypes_),
                      {},
                      std::move(docs_))
    {
    }

    ZL_Type inputType() const
    {
        return this->inputTypes[0];
    }

    void encode(PyEncoderCtx ctx) const override
    {
        auto stream = ctx.getInput(0);
        py::object input;
        if (stream.type() == ZL_Type_string) {
            input = stream.asList();
        } else {
            input = stream.asArray();
        }
        auto outputs = encode(ctx.asSimpleEncoderCtx(), std::move(input));
        for (size_t i = 0; i < outputs.size(); ++i) {
            ctx.createOutput(i, std::move(outputs[i]));
        }
    }

    void decode(PyDecoderCtx ctx) const override
    {
        auto streams = ctx.getFixedInputs();
        py::list inputs;
        for (auto&& pyStream : streams) {
            auto const& stream = pyStream.cast<PyStream const&>();
            if (stream.type() == ZL_Type_string) {
                inputs.append(stream.asList());
            } else {
                inputs.append(stream.asArray());
            }
        }
        auto output = decode(ctx.asSimpleDecoderCtx(), std::move(inputs));
        ctx.createOutput(0, std::move(output));
    }

    virtual py::tuple encode(PySimpleEncoderCtx ctx, py::array input) const = 0;

    virtual py::buffer decode(PySimpleDecoderCtx ctx, py::tuple inputs)
            const = 0;

    virtual ~PySimpleCustomTransform() {}
};

class PySimpleCustomTransformTrampoline : public PySimpleCustomTransform {
   public:
    using PySimpleCustomTransform::PySimpleCustomTransform;

    py::tuple encode(PySimpleEncoderCtx ctx, py::array input) const override
    {
        PYBIND11_OVERRIDE_PURE(
                py::tuple, PySimpleCustomTransform, encode, ctx, input);
    }

    py::buffer decode(PySimpleDecoderCtx ctx, py::tuple inputs) const override
    {
        PYBIND11_OVERRIDE_PURE(
                py::buffer, PySimpleCustomTransform, decode, ctx, inputs);
    }
};

class PyCustomTransformAdaptor : public CustomTransform {
   public:
    explicit PyCustomTransformAdaptor(py::object transform)
            : CustomTransform(castTransform(transform).id),
              transform_(std::move(transform))
    {
    }

    ZL_Report encode(
            ZL_Encoder* eictx,
            ZL_Input const* inputs[],
            size_t nbInputs) const override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        try {
            ZL_Report report;
            PyEncoderCtx ctx{ eictx, this, inputs, nbInputs, &report };
            transform().encode(ctx);
            ZL_ERR_IF_ERR(report);
            return ZL_returnValue(nbSuccessors());
        } catch (std::runtime_error const& err) {
            ZL_ERR(transform_executionFailure,
                   "Exception thrown: %s",
                   err.what());
        }
    }

    ZL_Report decode(
            ZL_Decoder* dictx,
            ZL_Input const* fixedInputs[],
            size_t nbFixedInputs,
            ZL_Input const* voInputs[],
            size_t nbVoInputs) const override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
        try {
            ZL_Report report;
            PyDecoderCtx ctx{ dictx,    this,       fixedInputs, nbFixedInputs,
                              voInputs, nbVoInputs, &report };
            transform().decode(ctx);
            ZL_ERR_IF_ERR(report);
            return ZL_returnValue(nbSuccessors());
        } catch (std::runtime_error const& err) {
            ZL_ERR(transform_executionFailure,
                   "Exception thrown: %s",
                   err.what());
        }
    }

    size_t nbInputs() const override
    {
        return transform().inputTypes.size();
    }

    size_t nbVariableSuccessors() const override
    {
        return transform().variableOutputTypes.size();
    }

    size_t nbSuccessors() const override
    {
        return transform().fixedOutputTypes.size() + nbVariableSuccessors();
    }

    ZL_Type inputType(size_t idx) const override
    {
        return transform().inputTypes[idx];
    }

    ZL_Type outputType(size_t idx) const override
    {
        size_t const nbFixed = nbFixedSuccessors();
        if (idx < nbFixed) {
            return transform().fixedOutputTypes.at(idx);
        } else {
            return transform().variableOutputTypes.at(idx - nbFixed);
        }
    }

    std::string description() const override
    {
        return transform().docs;
    }

   private:
    static PyCustomTransform& castTransform(py::object const& obj)
    {
        // py::isinstance doesn't work well with C++ inheritance
        if (py::isinstance<PyCustomTransform>(obj)) {
            return obj.cast<PyCustomTransform&>();
        } else if (py::isinstance<PySimpleCustomTransform>(obj)) {
            return obj.cast<PySimpleCustomTransform&>();
        } else {
            throw std::runtime_error{ "Unknown transform type!" };
        }
    }

    PyCustomTransform& transform() const
    {
        return castTransform(transform_);
    }

    py::object transform_;
};

class PyCustomSelector : public CustomSelector {
   public:
    PyCustomSelector(ZL_Type inputType, std::string docs)
            : inputType_(inputType), docs_(std::move(docs))
    {
    }

    ZL_Type inputType() const override
    {
        return inputType_;
    }

    virtual ZL_GraphID
    select(PySelectorCtx ctx, py::object input, py::tuple successors) const = 0;

    ZL_GraphID select(
            ZL_Selector const* selCtx,
            ZL_Input const* input,
            std::span<ZL_GraphID const> successors) const override
    {
        PySelectorCtx ctx(selCtx, inputType());
        PyStream pyStream(input);
        py::object pyInput;
        if (pyStream.type() == ZL_Type_string) {
            pyInput = pyStream.asList();
        } else {
            pyInput = pyStream.asArray();
        }
        py::tuple pySuccessors = py::cast(
                std::vector<ZL_GraphID>(successors.begin(), successors.end()));
        return select(ctx, std::move(pyInput), std::move(pySuccessors));
    }
    virtual ~PyCustomSelector() = default;

    ZL_Type const inputType_;
    std::string const docs_;
};

class PyCustomSelectorTrampoline : public PyCustomSelector {
   public:
    using PyCustomSelector::PyCustomSelector;

    ZL_GraphID select(PySelectorCtx ctx, py::object input, py::tuple successors)
            const override
    {
        PYBIND11_OVERRIDE_PURE(
                ZL_GraphID, PyCustomSelector, select, ctx, input, successors);
    }
};

class CustomSelectorAdaptor : public CustomSelector {
   public:
    explicit CustomSelectorAdaptor(py::object selector)
            : selector_(std::move(selector))
    {
    }

    ZL_Type inputType() const override
    {
        return selector().inputType();
    }

    std::string description() const override
    {
        return selector().description();
    }

    ZL_GraphID select(
            ZL_Selector const* selCtx,
            ZL_Input const* input,
            std::span<ZL_GraphID const> successors) const override
    {
        return selector().select(selCtx, input, successors);
    }

   private:
    CustomSelector& selector() const
    {
        return selector_.cast<CustomSelector&>();
    }

    py::object selector_;
};

class SharedGraph : public Graph {
   public:
    explicit SharedGraph(std::shared_ptr<Graph> graph)
            : graph_(std::move(graph))
    {
    }

    ZL_GraphID registerGraph(ZL_Compressor& cgraph) const override
    {
        return graph_->registerGraph(cgraph);
    }

    void registerGraph(ZL_DCtx& dctx) const override
    {
        return graph_->registerGraph(dctx);
    }

    ZL_Type inputType() const override
    {
        return graph_->inputType();
    }

    std::string description() const override
    {
        return graph_->description();
    }

   private:
    std::shared_ptr<Graph> graph_;
};

using PyGraphMap     = std::unordered_map<std::string, std::shared_ptr<Graph>>;
using PyTransformMap = std::unordered_map<std::string, py::object>;
using PySelectorMap  = std::unordered_map<std::string, py::object>;

GraphMap translateCustomGraphs(PyGraphMap const& pyMap)
{
    GraphMap map;
    map.reserve(pyMap.size());
    for (auto const& [name, graph] : pyMap) {
        map.emplace(name, std::make_unique<SharedGraph>(graph));
    }
    return map;
}

TransformMap translateCustomTransforms(PyTransformMap const& pyMap)
{
    TransformMap map;
    map.reserve(pyMap.size());
    for (auto const& [name, transform] : pyMap) {
        map.emplace(
                name, std::make_unique<PyCustomTransformAdaptor>(transform));
    }
    return map;
}

SelectorMap translateCustomSelectors(PySelectorMap const& pyMap)
{
    SelectorMap map;
    map.reserve(pyMap.size());
    for (auto const& [name, selector] : pyMap) {
        map.emplace(name, std::make_unique<CustomSelectorAdaptor>(selector));
    }
    return map;
}

std::shared_ptr<JsonGraph> constructJsonGraph(
        std::string_view jsonStr,
        ZL_Type inputType,
        std::optional<PyTransformMap> pyCustomTransforms,
        std::optional<PyGraphMap> pyCustomGraphs,
        std::optional<PySelectorMap> pyCustomSelectors)
{
    std::optional<TransformMap> customTransforms;
    if (pyCustomTransforms.has_value()) {
        customTransforms = translateCustomTransforms(*pyCustomTransforms);
    }
    std::optional<GraphMap> customGraphs;
    if (pyCustomGraphs.has_value()) {
        customGraphs = translateCustomGraphs(*pyCustomGraphs);
    }
    std::optional<SelectorMap> customSelectors;
    if (pyCustomSelectors.has_value()) {
        customSelectors = translateCustomSelectors(*pyCustomSelectors);
    }
    folly::dynamic json = folly::parseJson(jsonStr);
    return std::make_shared<JsonGraph>(
            std::move(json),
            inputType,
            std::move(customTransforms),
            std::move(customGraphs),
            std::move(customSelectors));
}

std::shared_ptr<JsonGraph> constructJsonGraphDict(
        py::dict json,
        ZL_Type inputType,
        std::optional<PyTransformMap> pyCustomTransforms,
        std::optional<PyGraphMap> pyCustomGraphs,
        std::optional<PySelectorMap> pyCustomSelectors)
{
    auto dumps          = py::module_::import("json").attr("dumps");
    std::string jsonStr = dumps(json).cast<std::string>();
    return constructJsonGraph(
            jsonStr,
            inputType,
            std::move(pyCustomTransforms),
            std::move(pyCustomGraphs),
            std::move(pyCustomSelectors));
}

py::bytes pyCompress(
        py::bytes const& data,
        Graph const& graph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    return compress(data.cast<std::string_view>(), graph, globalParams);
}

py::bytes pyCompressMulti(
        std::vector<py::bytes> const& data,
        Graph const& graph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    // inefficient but necessary for now
    std::vector<std::string_view> dataViews;
    dataViews.reserve(data.size());
    for (auto const& d : data) {
        dataViews.push_back(d.cast<std::string_view>());
    }
    return compressMulti(dataViews, graph, globalParams);
}

std::vector<py::bytes> pyDecompressMulti(
        py::bytes const& compressed,
        Graph const* graph)
{
    auto ret = (graph == nullptr)
            ? zstrong::decompressMulti(compressed.cast<std::string_view>())
            : zstrong::decompressMulti(
                      compressed.cast<std::string_view>(), *graph);
    std::vector<py::bytes> retBytes;
    retBytes.reserve(ret.size());
    for (auto const& r : ret) {
        retBytes.push_back(py::bytes(r));
    }
    return retBytes;
}

py::bytes pyDecompress(py::bytes const& compressed, Graph const* graph)
{
    auto ret = pyDecompressMulti(compressed, graph);
    if (ret.size() != 1) {
        throw std::runtime_error{ "Expected exactly one output stream" };
    }
    return ret[0];
}

size_t pyGetHeaderSize(py::bytes const& compressed)
{
    return zstrong::getHeaderSize(compressed.cast<std::string_view>());
}

std::vector<double> pyDecompressMeasureSpeedMultiple(
        std::vector<py::bytes> compressed,
        Graph const* graph)
{
    std::vector<std::string_view> compressedStrings;
    compressedStrings.reserve(compressed.size());
    for (auto const& c : compressed) {
        compressedStrings.push_back(c.cast<std::string_view>());
    }
    if (graph == nullptr) {
        return zstrong::measureDecompressionSpeeds(
                std::move(compressedStrings));
    } else {
        return zstrong::measureDecompressionSpeeds(
                std::move(compressedStrings), *graph);
    }
}

double pyDecompressMeasureSpeedOne(py::bytes compressed, Graph const* graph)
{
    if (graph == nullptr) {
        return zstrong::measureDecompressionSpeeds(
                { compressed.cast<std::string_view>() })[0];
    } else {
        return zstrong::measureDecompressionSpeeds(
                { compressed.cast<std::string_view>() }, *graph)[0];
    }
}

std::vector<py::array> pySplitExtractedStreamsImpl(
        std::string_view extractedStreams)
{
    auto streams = splitExtractedStreams(extractedStreams);
    std::vector<py::array> byteStreams;
    byteStreams.reserve(streams.size());
    for (auto const& stream : streams) {
        py::array array = pybind::toNumpyArray(
                stream.type,
                stream.nbElts,
                stream.eltWidth,
                stream.data.data());
        byteStreams.push_back(std::move(array));
    }

    return byteStreams;
}

std::vector<py::array> pySplitExtractedStreams(
        py::bytes const& extractedStreams)
{
    return pySplitExtractedStreamsImpl(
            extractedStreams.cast<std::string_view>());
}

std::vector<py::array> pyReadExtractedStreams(std::string const& path)
{
    std::string data;
    if (!folly::readFile(path.c_str(), data)) {
        throw std::runtime_error{ "Failed to read file: " + path };
    }
    return pySplitExtractedStreamsImpl(data);
}

std::string docstring(ParameterizedTransform const& transform)
{
    std::string docs = transform.description();

    docs += "\n\n";

    // TODO(csv): support multiple input types
    docs += "Input stream type: " + typeName(transform.inputType(0));
    docs += "\n\n";

    for (auto const& param : transform.intParams()) {
        docs += ":param ";
        docs += param.name;
        docs += ": ";
        docs += param.docs;
        docs += "\n";
    }

    for (auto const& param : transform.genericParams()) {
        docs += ":param ";
        docs += param.name;
        docs += ": ";
        docs += param.docs;
        docs += "\n";
    }

    size_t const nbSuccessors = transform.nbSuccessors();
    for (size_t i = 0; i < nbSuccessors; ++i) {
        docs += ":param ";
        docs += transform.successorName(i);
        docs += ": Successor of StreamType "
                + typeName(transform.outputType(i));
        docs += "\n";
    }
    return docs;
}

std::string docstring(Graph const& graph)
{
    std::string docs = graph.description();

    docs += "\n\n";

    docs += "Input stream type: " + typeName(graph.inputType());

    return docs;
}

std::string docstring(
        Selector const& selector,
        std::string const& extraArgs = "")
{
    std::string docs = selector.description();

    docs += "\n\n";

    docs += "Input stream type: " + typeName(selector.inputType());

    docs += "\n\n";

    docs += extraArgs;
    docs += ":param *args: The possible successor graphs.\n";

    return docs;
}

py::dict transformFnImpl(
        std::string const& name,
        std::vector<py::dict> successors,
        std::unordered_map<int, int> intParams             = {},
        std::unordered_map<int, std::string> genericParams = {})
{
    auto transform = py::dict(
            py::arg("name")          = name,
            py::arg("successors")    = successors,
            py::arg("int_params")    = intParams,
            py::arg("binary_params") = genericParams);
    return transform;
}

template <size_t>
using ToInt = int;
template <size_t>
using ToPyDict = py::dict;
template <size_t>
using ToString = std::string;

/**
 * Template that defines a transform with IntParams... integer params,
 * GenericParams... generic params, and Successors... successor params.
 *
 * We expect the IntParams pack to contain one entry for each integer
 * param, the GenericParams pack to contain one entry for each generic param,
 * and the Successors pack to contain one entry for each successor param. These
 * are passed as `index_sequence` so that we can have multiple parameter packs.
 * This is an awkward API, so we wrap this in helper functions that make it
 * easier (see below).
 *
 * E.g. a function with 2 int params, 1 generic param, and 3 successors would
 * have: IntParams = 0, 1 GenericParams = 0, Successors = 0, 1, 2.
 */
template <size_t... IntParams, size_t... GenericParams, size_t... Successors>
void defTransformInner4(
        py::module_& m,
        std::string name,
        ParameterizedTransform const& transform,
        std::index_sequence<IntParams...>,
        std::index_sequence<GenericParams...>,
        std::index_sequence<Successors...>)
{
    auto const& intParams = transform.intParams();
    std::vector<int> intParamKeys;
    for (auto const& param : intParams) {
        intParamKeys.push_back(param.key);
    }
    std::vector<int> genericParamKeys;
    for (auto const& param : transform.genericParams()) {
        genericParamKeys.push_back(param.key);
    }
    // Lambda that takes sizeof...(IntParams) integer params,
    // sizeof...(GenericParams) generic params, and sizeof...(Successors)
    // successors. We need to accept the exact right number of parameters so
    // pybind knows how many arguments we are taking.
    auto transformFn = [name = name, intParamKeys, genericParamKeys](
                               ToInt<IntParams>... intParams,
                               ToString<GenericParams>... genericParams,
                               ToPyDict<Successors>... successors) {
        std::vector<int> intParamValues{ intParams... };
        std::unordered_map<int, int> intParamMap;
        for (size_t i = 0; i < intParamKeys.size(); ++i) {
            intParamMap.emplace(intParamKeys[i], intParamValues[i]);
        }
        std::vector<std::string> genericParamValues{ genericParams... };
        std::unordered_map<int, std::string> genericParamMap;
        for (size_t i = 0; i < genericParamKeys.size(); ++i) {
            genericParamMap.emplace(genericParamKeys[i], genericParamValues[i]);
        }
        return transformFnImpl(
                name,
                std::vector<py::dict>{ successors... },
                std::move(intParamMap),
                std::move(genericParamMap));
    };

    m.def(name.c_str(),
          std::move(transformFn),
          py::doc(docstring(transform).c_str()),
          // Document each arg (fold expr unpacks for each index in IntParams)
          py::arg(transform.intParams()[IntParams].name.c_str())...,
          // Document each arg (fold expr unpacks for each index in
          // GenericParams)
          py::arg(transform.genericParams()[GenericParams].name.c_str())...,
          // Document each arg (fold expr unpacks for each index in Successors)
          py::arg(transform.successorName(Successors).c_str())...);
}

/// Helper to make it easier to call defTransformInner4() without creating
/// std::index_sequence containers. It creates the correct index sequences
/// given the number of kIntParams, kGenericParams, and kSuccessorParams.
template <size_t kIntParams, size_t kGenericParams, size_t kSuccessorParams>
void defTransformInner3(
        py::module& m,
        std::string const& name,
        ParameterizedTransform const& transform)
{
    defTransformInner4(
            m,
            name,
            transform,
            std::make_index_sequence<kIntParams>{},
            std::make_index_sequence<kGenericParams>{},
            std::make_index_sequence<kSuccessorParams>{});
}

/// Dispatches runtime #successors to compile time specialization.
template <size_t kIntParams, size_t kGenericParams>
void defTransformInner2(
        py::module& m,
        std::string const& name,
        ParameterizedTransform const& transform)
{
    if (transform.nbSuccessors() == 1) {
        defTransformInner3<kIntParams, kGenericParams, 1>(m, name, transform);
    } else if (transform.nbSuccessors() == 2) {
        defTransformInner3<kIntParams, kGenericParams, 2>(m, name, transform);
    } else if (transform.nbSuccessors() == 3) {
        defTransformInner3<kIntParams, kGenericParams, 3>(m, name, transform);
    } else if (transform.nbSuccessors() == 4) {
        defTransformInner3<kIntParams, kGenericParams, 4>(m, name, transform);
    } else if (transform.nbSuccessors() == 5) {
        defTransformInner3<kIntParams, kGenericParams, 5>(m, name, transform);
    } else if (transform.nbSuccessors() == 6) {
        defTransformInner3<kIntParams, kGenericParams, 6>(m, name, transform);
    } else if (transform.nbSuccessors() == 7) {
        defTransformInner3<kIntParams, kGenericParams, 7>(m, name, transform);
    } else if (transform.nbSuccessors() == 8) {
        defTransformInner3<kIntParams, kGenericParams, 8>(m, name, transform);
    } else if (transform.nbSuccessors() == 9) {
        defTransformInner3<kIntParams, kGenericParams, 9>(m, name, transform);
    } else if (transform.nbSuccessors() == 10) {
        defTransformInner3<kIntParams, kGenericParams, 10>(m, name, transform);
    } else if (transform.nbSuccessors() == 11) {
        defTransformInner3<kIntParams, kGenericParams, 11>(m, name, transform);
    } else if (transform.nbSuccessors() == 12) {
        defTransformInner3<kIntParams, kGenericParams, 12>(m, name, transform);
    } else if (transform.nbSuccessors() == 13) {
        defTransformInner3<kIntParams, kGenericParams, 13>(m, name, transform);
    } else if (transform.nbSuccessors() == 14) {
        defTransformInner3<kIntParams, kGenericParams, 14>(m, name, transform);
    } else if (transform.nbSuccessors() == 15) {
        defTransformInner3<kIntParams, kGenericParams, 15>(m, name, transform);
    } else if (transform.nbSuccessors() == 16) {
        defTransformInner3<kIntParams, kGenericParams, 16>(m, name, transform);
    } else {
        throw std::runtime_error(
                "Need to extend function to support more successors");
    }
}

/// Dispatches runtime #successors to compile time specialization.
template <size_t kIntParams>
void defTransformInner1(
        py::module& m,
        std::string const& name,
        ParameterizedTransform const& transform)
{
    if (transform.genericParams().size() == 0) {
        defTransformInner2<kIntParams, 0>(m, name, transform);
    } else if (transform.genericParams().size() == 1) {
        defTransformInner2<kIntParams, 1>(m, name, transform);
    } else if (transform.genericParams().size() == 2) {
        defTransformInner2<kIntParams, 2>(m, name, transform);
    } else {
        throw std::runtime_error(
                "Need to extend function to support more genericParams");
    }
}

void defTransform(
        py::module& m,
        std::string const& name,
        ParameterizedTransform const& transform)
{
    if (transform.intParams().size() == 0) {
        defTransformInner1<0>(m, name, transform);
    } else if (transform.intParams().size() == 1) {
        defTransformInner1<1>(m, name, transform);
    } else if (transform.intParams().size() == 2) {
        defTransformInner1<2>(m, name, transform);
    } else {
        throw std::runtime_error(
                "Need to extend function to support more int_params");
    }
}

void defSelector(
        py::module& m,
        std::string const& name,
        Selector const& selector)
{
    if (name == "extract") {
        // Special case this one until we get another with params to generalize
        // it.
        std::string path =
                ":param path: The directory that we should extract data to.\n";
        m.def(
                "extract",
                [&](std::string path, py::dict successor) {
                    std::vector<py::dict> successors{ std::move(successor) };
                    std::unordered_map<int, std::string> stringParams;
                    stringParams.emplace(1, std::move(path));
                    return py::dict(
                            py::arg("name")          = name,
                            py::arg("successors")    = successors,
                            py::arg("string_params") = stringParams);
                },
                py::doc(docstring(selector, path).c_str()),
                py::arg("path"),
                py::arg("successor"));
        return;
    }
    m.def(
            name.c_str(),
            [&](py::args successors) {
                return py::dict(
                        py::arg("name") = name,
                        py::arg("successors") =
                                successors.cast<std::vector<py::dict>>());
            },
            py::doc(docstring(selector).c_str()));
}
} // namespace
} // namespace zstrong

PYBIND11_MODULE(zstrong_json, m)
{
    using namespace zstrong;

    m.doc() =
            "Python bindings for the zstrong JSON API.\n "
            "See docs for JsonGraph, compress, decompress, graphs, transforms, and selectors.\n "
            "For more examples checkout zstrong/tools/py/tests/test_zstrong_pybind.py";

    py::enum_<ZL_Type>(m, "StreamType")
            .value("serialized", ZL_Type_serial)
            .value("numeric", ZL_Type_numeric)
            .value("fixed_size_field", ZL_Type_struct)
            .value("variable_size_field", ZL_Type_string);

    // Graphs are stored as shared_ptr on the Python side.
    // All graphs are C++ objects, so keeping the graph alive is all that is
    // necessary. If graphs can be defined in Python like CustomTransforms, then
    // this wouldn't work.
    py::class_<Graph, std::shared_ptr<Graph>>(m, "Graph")
            .def("input_type", &Graph::inputType);

    char const* jsonGraphDoc =
            "A JsonGraph object takes a json graph description as a string or "
            "dict, and optionally custom transforms/graphs/selectors, and "
            "produces a Zstrong graph.\n\n"
            "The keys allowed in the json graph description are:\n\n"
            "\tname - The name of the transform/selector/graph. It should "
            "either be a standard transform/selector/graph, or provided in the "
            "custom transform/selector/graph map.\n\n"
            "\tsuccessors - The list of successors of the transform/selector. "
            "Graphs have no successors. Each successor is another json graph "
            "description.\n\n"
            "\tint_params - Optionally a map from int param key to int param "
            "value.\n\n"
            "\tstring_params - Optionally a map from int param key to string "
            "param value.\n\n"
            "\tbinary_params - Optionally a map from int param key to base64 "
            "encoded string param value.\n\n";

    py::class_<JsonGraph, Graph, std::shared_ptr<JsonGraph>>(m, "JsonGraph")
            .def(py::init(&constructJsonGraph),
                 py::arg("json"),
                 py::arg("input_type")        = ZL_Type_serial,
                 py::arg("custom_transforms") = std::nullopt,
                 py::arg("custom_graphs")     = std::nullopt,
                 py::arg("custom_selectors")  = std::nullopt)
            .def(py::init(&constructJsonGraphDict),
                 py::arg("json"),
                 py::arg("input_type")        = ZL_Type_serial,
                 py::arg("custom_transforms") = std::nullopt,
                 py::arg("custom_graphs")     = std::nullopt,
                 py::arg("custom_selectors")  = std::nullopt)
            .doc() = jsonGraphDoc;

    m.def("compress",
          &pyCompress,
          py::doc("Compress data using graph"),
          py::arg("data"),
          py::arg("graph"),
          py::arg("global_params") = std::nullopt);
    m.def("compress_multi",
          &pyCompressMulti,
          py::doc("Compress multiple data buffers using a multi-input graph"),
          py::arg("data"),
          py::arg("graph"),
          py::arg("global_params") = std::nullopt);

    m.def("decompress",
          &pyDecompress,
          py::doc("Decompress compressed into a single regenerated stream using graph. The graph can be omitted if there were no custom transforms."),
          py::arg("compressed"),
          py::arg("graph") = nullptr);
    m.def("decompress_multi",
          &pyDecompressMulti,
          py::doc("Decompress compressed into multiple regenerated streams using graph. The graph can be omitted if there were no custom transforms."),
          py::arg("compressed"),
          py::arg("graph") = nullptr);

    m.def("get_header_size",
          &pyGetHeaderSize,
          py::doc("Get the size of the header in bytes."),
          py::arg("compressed"));
    m.def("measure_decompress_speed",
          &pyDecompressMeasureSpeedOne,
          py::doc("Decompress compressed input using graph and measures how much time decompression takes, returns Mbps. The graph can be omitted if there were no custom transforms."),
          py::arg("compressed"),
          py::arg("graph") = nullptr);
    m.def("measure_decompress_speed_multiple",
          &pyDecompressMeasureSpeedMultiple,
          py::doc("Decompress multiple compressed inputs using graph and measures how much time decompression takes, returns Mbps. The graph can be omitted if there were no custom transforms."),
          py::arg("compressed"),
          py::arg("graph") = nullptr);
    m.def("split_extracted_streams",
          &pySplitExtractedStreams,
          py::arg("streams"),
          py::doc("Split the extracted streams as np.array from the extracted file contents."));
    m.def("read_extracted_streams",
          &pyReadExtractedStreams,
          py::arg("path"),
          py::doc("Read the extracted streams from path and return them as np.array."));

    py::enum_<ZL_CParam>(m, "GCParam")
            .value("compression_level", ZL_CParam_compressionLevel)
            .value("decompression_level", ZL_CParam_decompressionLevel)
            .value("format_version", ZL_CParam_formatVersion);

    py::class_<PyStream>(m, "Stream")
            .def("type", &PyStream::type)
            .def("as_array", &PyStream::asArray)
            .def("as_bytes", &PyStream::asBytes)
            .def("as_list", &PyStream::asList);

    py::class_<PyEncoderCtx>(m, "EncoderCtx")
            .def("get_global_param", &PyEncoderCtx::getGlobalParam)
            .def("get_local_int_param", &PyEncoderCtx::getLocalIntParam)
            .def("get_local_string_param", &PyEncoderCtx::getLocalStringParam)
            .def("get_local_binary_param", &PyEncoderCtx::getLocalBinaryParam)
            .def("send_transform_header", &PyEncoderCtx::sendTransformHeader)
            .def("get_inputs", &PyEncoderCtx::getInputs)
            .def("get_input", &PyEncoderCtx::getInput)
            .def("create_output", &PyEncoderCtx::createOutput);

    py::class_<PySimpleEncoderCtx>(m, "SimpleEncoderCtx")
            .def("get_global_param", &PySimpleEncoderCtx::getGlobalParam)
            .def("get_local_int_param", &PySimpleEncoderCtx::getLocalIntParam)
            .def("get_local_string_param",
                 &PySimpleEncoderCtx::getLocalStringParam)
            .def("get_local_binary_param",
                 &PySimpleEncoderCtx::getLocalBinaryParam)
            .def("send_transform_header",
                 &PySimpleEncoderCtx::sendTransformHeader);

    py::class_<PySelectorCtx>(m, "SelectorCtx")
            .def("get_global_param", &PySelectorCtx::getGlobalParam)
            .def("get_local_int_param", &PySelectorCtx::getLocalIntParam)
            .def("get_local_string_param", &PySelectorCtx::getLocalStringParam)
            .def("get_local_binary_param", &PySelectorCtx::getLocalBinaryParam)
            .def("try_graph", &PySelectorCtx::tryGraph);

    py::class_<PyDecoderCtx>(m, "DecoderCtx")
            .def("get_transform_header", &PyDecoderCtx::getTransformHeader)
            .def("get_fixed_inputs", &PyDecoderCtx::getFixedInputs)
            .def("get_variable_inputs", &PyDecoderCtx::getVariableInputs)
            .def("create_output", &PyDecoderCtx::createOutput);

    py::class_<PySimpleDecoderCtx>(m, "SimpleDecoderCtx")
            .def("get_transform_header",
                 &PySimpleDecoderCtx::getTransformHeader);

    // Allows writing custom transforms in Python.
    // When storing this class in C++ you must store the py::object to keep the
    // Python object alive. This also means that the object that owns the
    // CustomTransform (the JsonGraph) must be destroyed on a Python thread, so
    // that the GIL is held. This can be worked around if needed.
    py::class_<PyCustomTransform, PyCustomTransformTrampoline>(
            m, "CustomTransform")
            .def(py::init<
                         ZL_IDType,
                         std::vector<ZL_Type>,
                         std::vector<ZL_Type>,
                         std::vector<ZL_Type>,
                         std::string>(),
                 py::arg("id"),
                 py::arg("input_types"),
                 py::arg("fixed_output_types"),
                 py::arg("variable_output_types") = std::vector<ZL_Type>{},
                 py::arg("docs")                  = "")
            .def_readonly("id", &PyCustomTransform::id)
            .def_readonly("input_types", &PyCustomTransform::inputTypes)
            .def_readonly(
                    "fixed_output_types", &PyCustomTransform::fixedOutputTypes)
            .def_readonly(
                    "variable_output_types",
                    &PyCustomTransform::variableOutputTypes)
            .def_readonly("docs", &PyCustomTransform::docs)
            .def("encode", &PyCustomTransform::encode)
            .def("decode", &PyCustomTransform::decode);
    py::class_<PySimpleCustomTransform, PySimpleCustomTransformTrampoline>(
            m, "SimpleCustomTransform")
            .def(py::init<
                         ZL_IDType,
                         ZL_Type,
                         std::vector<ZL_Type>,
                         std::string>(),
                 py::arg("id"),
                 py::arg("input_type"),
                 py::arg("output_types"),
                 py::arg("docs") = "")
            .def_readonly("id", &PySimpleCustomTransform::id)
            .def_readonly(
                    "output_types", &PySimpleCustomTransform::fixedOutputTypes)
            .def_readonly("docs", &PySimpleCustomTransform::docs)
            .def("input_type", &PySimpleCustomTransform::inputType)
            .def("encode",
                 static_cast<py::tuple (PySimpleCustomTransform::*)(
                         PySimpleEncoderCtx, py::array) const>(
                         &PySimpleCustomTransform::encode))
            .def("decode",
                 static_cast<py::buffer (PySimpleCustomTransform::*)(
                         PySimpleDecoderCtx, py::tuple) const>(
                         &PySimpleCustomTransform::decode));

    py::class_<ZL_GraphID>(m, "GraphID");

    // Allows writing custom selectors.
    py::class_<CustomSelector, std::unique_ptr<CustomSelector>>(
            m, "BaseCustomSelector")
            .doc() =
            "Base type for Zstrong CustomSelectors, can be used as a type hint.";
    ;
    py::class_<
            PyCustomSelector,
            PyCustomSelectorTrampoline,
            CustomSelector,
            std::unique_ptr<PyCustomSelector>>(m, "CustomSelector")
            .def(py::init<ZL_Type, std::string>(),
                 py::arg("input_type"),
                 py::arg("docs") = "")
            .def("select",
                 (ZL_GraphID (PyCustomSelector::*)(
                         PySelectorCtx, py::object, py::tuple)
                          const)(&PyCustomSelector::select));

    // Module containing all the standard graphs
    {
        auto g = m.def_submodule("graphs");
        g.doc() =
                "Zstrong's standard graphs. Run graphs.list() to see a list of graphs.\n"
                "Each graph is a function in this module that takes no arguments.";

        g.def(
                "list",
                [] {
                    std::vector<std::string> graphs;
                    for (auto const& [name, _] : getStandardGraphs()) {
                        graphs.push_back(name);
                    }
                    return graphs;
                },
                py::doc("Lists all the available graphs. Each graph is a function in this module."));

        for (auto const& [name, graph] : getStandardGraphs()) {
            g.def(
                    name.c_str(),
                    [name = name] {
                        std::unordered_map<std::string, std::string> json;
                        json["name"] = name;
                        return json;
                    },
                    py::doc(docstring(*graph).c_str()));
        }
    }

    {
        auto t = m.def_submodule("transforms");
        t.doc() =
                "Zstrong's standard transforms. Run transforms.list() to see a list of transforms.\n"
                "Each transform is a function in this module. See each transforms docstring for details.";

        t.def(
                "list",
                [] {
                    std::vector<std::string> transforms;
                    for (auto const& [name, _] : getStandardTransforms()) {
                        transforms.push_back(name);
                    }
                    return transforms;
                },
                py::doc("Lists all the available transforms. Each transform is a function in this module."));

        for (auto const& [name, transform] : getStandardTransforms()) {
            defTransform(t, name, *transform);
        }
    }

    {
        auto s = m.def_submodule("selectors");
        s.doc() =
                "Zstrong's standard selectors. Run selectors.list() to see a list of selectors.\n"
                "Each selectors is a function in this module. See each selectors docstring for details.";

        s.def(
                "list",
                [] {
                    std::vector<std::string> selectors;
                    for (auto const& [selector, _] : getStandardSelectors()) {
                        selectors.push_back(selector);
                    }
                    return selectors;
                },
                py::doc("Lists all the available selectors. Each selector is a function in this module."));

        for (auto const& [name, selector] : getStandardSelectors()) {
            defSelector(s, name, *selector);
        }
    }

    pybind::initMlSubmodule(m);
}
