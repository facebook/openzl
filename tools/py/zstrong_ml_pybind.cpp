// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <memory>

#include "openzl/common/stream.h"
#include "openzl/zl_data.h"
#include "tools/py/pybind_helpers.h"
#include "tools/py/zstrong_ml_pybind.h"
#include "tools/zstrong_ml.h"

namespace py = pybind11;

namespace zstrong::pybind {
using namespace zstrong::ml;

namespace {
class PyFeatureGenerator : public FeatureGenerator {
   public:
    using FeatureGenerator::getFeatures;
    explicit PyFeatureGenerator(
            const std::unordered_set<std::string>& featureNames)
            : FeatureGenerator(featureNames)
    {
    }

    virtual ~PyFeatureGenerator()                        = default;
    virtual FeatureMap getFeatures(py::array data) const = 0;
    std::unordered_set<std::string> getFeatureNames() const override
    {
        return {};
    }

    void getFeatures(
            FeatureMap& featuresMap,
            const void* data,
            ZL_Type type,
            size_t eltWidth,
            size_t nbElts) const override
    {
        auto pyFeaturesMap =
                getFeatures(toNumpyArray(type, nbElts, eltWidth, data));
        featuresMap.insert(pyFeaturesMap.begin(), pyFeaturesMap.end());
    }
};

class PyFeatureGeneratorTrampoline : public PyFeatureGenerator {
   public:
    using PyFeatureGenerator::PyFeatureGenerator;

    FeatureMap getFeatures(py::array data) const override
    {
        PYBIND11_OVERRIDE_PURE(
                FeatureMap, PyFeatureGenerator, getFeatures, data);
    }
};

py::array sampleDataToArray(MLTrainingSampleData const& sd)
{
    return toNumpyArray(
            sd.streamType,
            sd.data.size() / sd.eltWidth,
            sd.eltWidth,
            sd.data.data());
};
} // namespace

void initMlSubmodule(py::module& m)
{
    auto s = m.def_submodule("ml");

    s.doc() = "Zstrong's ML selectors.";

    /*
     * Models
     */
    py::class_<MLModel, std::shared_ptr<MLModel>>(s, "MLModel").doc() =
            "Base type for Zstrong ML Models, can be used as a type hint.";
    py::class_<
            GBTModel /* class */,
            MLModel /* base class */,
            std::shared_ptr<GBTModel> /* container */
            >(s, "GBTModel")
            .def(py::init<std::string_view>())
            .doc() = "GBT based model that can be used by an MLSelector.";

    /*
     * Feature Generators
     */
    {
        using namespace features;
        py::class_<FeatureGenerator, std::shared_ptr<FeatureGenerator>>(
                s, "BaseFeatureGenerator")
                .doc() =
                "Base type for Zstrong feature generators, can be used as a type hint.";
        py::class_<
                PyFeatureGenerator,
                PyFeatureGeneratorTrampoline,
                FeatureGenerator,
                std::shared_ptr<PyFeatureGenerator>>(s, "FeatureGenerator")
                .def(py::init<std::unordered_set<std::string>>(),
                     py::arg("feature_names"))
                .def("getFeatures",
                     (FeatureMap (PyFeatureGenerator::*)(py::array) const)
                             & PyFeatureGenerator::getFeatures,
                     py::arg("data"))
                .doc() =
                "Allows the creation of pythonic feature generator that can be used by `MLSelector`s and `MLTrainingSelector`s.\n"
                "Inheritors must implement the `getFeatures` method that accepts a data stream and returns a map of strings to floats.";

        auto getFeaturesNumericArray = [](const FeatureGenerator& self,
                                          py::array arr) {
            auto stream = arrayToStream(arr, ZL_Type_numeric);
            FeatureMap ret;
            self.getFeatures(
                    ret, ZL_codemodDataAsInput(ZL_codemodOutputAsData(stream)));
            STREAM_free(ZL_codemodOutputAsData(stream));
            return ret;
        };

        py::class_<
                features::IntFeatureGenerator,
                FeatureGenerator,
                std::shared_ptr<IntFeatureGenerator>>(s, "IntFeatureGenerator")
                .def(py::init<>())
                .def("getFeatures", getFeaturesNumericArray)
                .doc() =
                "Calculates basic features for numeric data, it assumes the data is unsigned integers";
        py::class_<
                features::DeltaIntFeatureGenerator,
                FeatureGenerator,
                std::shared_ptr<DeltaIntFeatureGenerator>>(
                s, "DeltaIntFeatureGenerator")
                .def(py::init<>())
                .def("getFeatures", getFeaturesNumericArray)
                .doc() =
                "Calculates basic integer features on the deltas of items in the stream";
        ;
        py::class_<
                features::TokenizeIntFeatureGenerator,
                FeatureGenerator,
                std::shared_ptr<TokenizeIntFeatureGenerator>>(
                s, "TokenizeIntFeatureGenerator")
                .def(py::init<>())
                .def("getFeatures", getFeaturesNumericArray)
                .doc() =
                "Calculates features that should help in a decision about tokenization";
    }

    /*
     * MLSelector
     */
    py::class_<MLSelector, CustomSelector, std::unique_ptr<MLSelector>>(
            s, "MLSelector")
            .def(py::init<
                         ZL_Type,
                         // The `shared_ptr` only ensures the CPP objects'
                         // lifetime the Pythonic side of the object may be
                         // released even while these `shared_ptr`s are held.
                         // This is a Pybind issue, which sadly has no good
                         // resolution right now.
                         // In order to maintain the lifetime of the Pythonic
                         // objects we also specify `py::keep_alive` later on in
                         // the definition.
                         std::shared_ptr<MLModel>,
                         std::shared_ptr<FeatureGenerator>,
                         std::vector<std::string>>(),
                 py::keep_alive<1, 3>(),
                 py::keep_alive<1, 4>(),
                 py::arg("input_type"),
                 py::arg("model"),
                 py::arg("feature_generator"),
                 py::arg("labels") = std::vector<std::string>());

    /*
     * MLTrainingSelector
     */
    py::class_<
            MemMLTrainingSelector,
            CustomSelector,
            std::unique_ptr<MemMLTrainingSelector>>(s, "MLTrainingSelector")
            .def(py::init<
                         ZL_Type,
                         std::vector<std::string>,
                         bool,
                         std::shared_ptr<FeatureGenerator>>(),
                 py::arg("input_type"),
                 py::arg("labels"),
                 py::arg("collect_inputs")    = true,
                 py::arg("feature_generator") = nullptr,
                 py::keep_alive<1, 4>(),
                 py::doc("A selector used to collect training data for MLSelector"))
            .def("get_collected_json",
                 &MemMLTrainingSelector::getCollectedJson,
                 py::doc("Returns JSON representation of collected samples"))
            .def("get_collected",
                 &MemMLTrainingSelector::getCollected,
                 py::doc("Returns all samples collected"))
            .def("flush_collected",
                 &MemMLTrainingSelector::flushCollected,
                 py::doc("Returns all samples collected and clears the memory"))
            .def("clear_collected",
                 &MemMLTrainingSelector::clearCollected,
                 py::doc("Clears memory of collected samples"));

    py::class_<MLTrainingSample>(s, "MLTrainingSample")
            .def_property_readonly(
                    "data",
                    [](const MLTrainingSample& s) -> std::optional<py::array> {
                        if (!s.data)
                            return std::nullopt;
                        return std::make_optional(sampleDataToArray(*s.data));
                    })
            .def_readonly("targets", &MLTrainingSample::targets)
            .def_readonly("features", &MLTrainingSample::features);
    s.def("samples_to_json",
          &MLTrainingSamplesToJson,
          "Serializes a list of `MLTrainingSample` as JSON");
    s.def("samples_from_json",
          &MLTrainingSamplesFromJson,
          "Deserializes a list of `MLTrainingSample` from JSON");
}

} // namespace zstrong::pybind
