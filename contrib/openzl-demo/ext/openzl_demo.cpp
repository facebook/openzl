// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <span>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include "openzl/openzl.hpp"
#include "tools/training/train.h"
#include "tools/training/utils/utils.h"

namespace {
namespace nb = nanobind;

std::unique_ptr<openzl::Compressor> createCompressor()
{
    auto compressor = std::make_unique<openzl::Compressor>();
    compressor->selectStartingGraph(openzl::graphs::ACE()(*compressor));
    return compressor;
}

std::vector<nb::bytes> train(
        const std::vector<nb::bytes>& inputs,
        std::optional<size_t> numThreads,
        std::optional<size_t> maxTimeSecs)
{
    std::vector<openzl::training::MultiInput> multiInputs;
    multiInputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        std::vector<openzl::Input> inputsVec;
        inputsVec.push_back(
                openzl::Input::refSerial(input.data(), input.size()));
        multiInputs.emplace_back(std::move(inputsVec));
    }
    openzl::Compressor compressor;
    compressor.selectStartingGraph(openzl::graphs::ACE()(compressor));
    compressor.setParameter(
            openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    openzl::training::TrainParams trainParams = {
        .compressorGenFunc =
                [](std::string_view serialized) {
                    auto compressor = std::make_unique<openzl::Compressor>();
                    compressor->deserialize(serialized);
                    return compressor;
                },
        .threads        = numThreads,
        .maxTimeSecs    = maxTimeSecs,
        .paretoFrontier = true,
    };

    auto trainedCompressors =
            openzl::training::train(multiInputs, compressor, trainParams);

    std::vector<nb::bytes> result;
    result.reserve(trainedCompressors.size());
    for (const auto& trainedCompressor : trainedCompressors) {
        result.emplace_back(
                (const uint8_t*)trainedCompressor->data(),
                trainedCompressor->size());
    }
    return result;
}

} // namespace

NB_MODULE(_openzl_demo, m)
{
    m.def("train",
          &train,
          nanobind::arg("inputs"),
          nanobind::arg("num_threads")   = std::nullopt,
          nanobind::arg("max_time_secs") = std::nullopt,
          "Train a compressor on the given inputs and return a Pareto frontier of trained compressors.");
}
