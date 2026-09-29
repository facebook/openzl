// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "custom_parsers/dependency_registration.h"
#include "openzl/codecs/zl_ace.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_reflection.h"

namespace {

std::string readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(std::string("Failed to open ") + path);
    }
    return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
}

void writeFile(const char* path, const std::string& contents)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error(std::string("Failed to open ") + path);
    }
    output.write(contents.data(), contents.size());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        return 1;
    }

    const std::string serialized = readFile(argv[1]);
    const std::string bundle     = readFile(argv[2]);
    auto compressor = openzl::custom_parsers::createCompressorFromSerialized(
            serialized, bundle);

    ZL_GraphID dictGraph;
    if (!ZL_Compressor_getStartingGraphID(compressor->get(), &dictGraph)) {
        throw std::runtime_error("Failed to get dictionary graph");
    }

    const ZL_GraphID trainableGraph = ZL_Compressor_buildACEGraphWithDefault(
            compressor->get(), dictGraph);
    if (!ZL_GraphID_isValid(trainableGraph)) {
        throw std::runtime_error("Failed to build ACE graph");
    }
    compressor->selectStartingGraph(trainableGraph);
    writeFile(argv[3], compressor->serialize());
    return 0;
}
