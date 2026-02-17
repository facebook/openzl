// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/Compressor.hpp"

#include "security/lionhead/utils/lib_ftest/ftest.h" // @manual
#include "tests/datagen/structures/CompressorProducer.h"
#include "tests/fuzz_utils.h"

namespace openzl::tests {
namespace {

FUZZ(CompressorSerializationTest, FuzzDeserialization)
{
    std::string input = gen_str(f, "input_data", InputLengthInBytes(1));

    try {
        Compressor compressor;
        compressor.deserialize(input);
    } catch (const Exception&) {
        // Ignore the exception as long as ZL_Result is returned since it is
        // valid to deserialize unsucessfully
    }
}

FUZZ(CompressorSerializationTest, FuzzRandomCompressorDeserializesSuccessfully)
{
    datagen::DataGen dg = fromFDP(f);
    auto rw             = dg.getRandWrapper();
    auto producer       = datagen::CompressorProducer(rw);
    auto zlCompressor   = producer.make();
    CompressorRef compressor(zlCompressor.get());
    auto serialized = compressor.serialize();
    compressor.deserialize(serialized);
}
} // namespace
} // namespace openzl::tests
