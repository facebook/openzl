// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/benchmark_config.h"

namespace {
static std::vector<std::string> k_predefinedShortList = {
    "E2E / Constant / Constant(nbElts=100000, eltWidth=16) / Compress",
    "E2E / Constant / Constant(nbElts=100000, eltWidth=16) / Decompress",
    "E2E / DispatchFixedSizeSegments / Uniform8(min=0, max=7, size=10240, seed=10) / Compress",
    "E2E / DispatchFixedSizeSegments / Uniform8(min=0, max=7, size=10240, seed=10) / Decompress",
    "E2E / DispatchVaryingSizedSegments / Uniform8(size=10240, seed=10) / Compress",
    "E2E / DispatchVaryingSizedSegments / Uniform8(size=10240, seed=10) / Decompress",
    "E2E / FSE / MostlyConstant / Compress",
    "E2E / FSE / MostlyConstant / Decompress",
    "E2E / FSE / Normal32(mean=128, stddev=10, size=10240, seed=10) / Compress",
    "E2E / FSE / Normal32(mean=128, stddev=10, size=10240, seed=10) / Decompress",
    // FieldLz compression testcases currently disabled due to instability.
    // @todo: understand the source of the instability, and re-enable them
    // "E2E / FieldLz16(clvl=0, dlvl=0) / Normal16(mean=32767, stddev=1024,
    // size=10240, seed=10) / Compress",
    "E2E / FieldLz16(clvl=0, dlvl=0) / Normal16(mean=32767, stddev=1024, size=10240, seed=10) / Decompress",
    // "E2E / FieldLz16(clvl=0, dlvl=0) / Uniform16(card=100, size=10240,
    // seed=10) / Compress",
    "E2E / FieldLz16(clvl=0, dlvl=0) / Uniform16(card=100, size=10240, seed=10) / Decompress",
    // "E2E / FieldLz16(clvl=3, dlvl=1) / Normal16(mean=32767, stddev=1024,
    // size=10240, seed=10) / Compress", "E2E / FieldLz16(clvl=3, dlvl=1) /
    // Uniform16(card=100, size=10240, seed=10) / Compress",
    "E2E / FieldLz16(clvl=3, dlvl=1) / Uniform16(card=100, size=10240, seed=10) / Decompress",
    // "E2E / FieldLz16(clvl=3, dlvl=7) / Normal16(mean=32767, stddev=1024,
    // size=10240, seed=10) / Compress",
    "E2E / FieldLz16(clvl=3, dlvl=7) / Normal16(mean=32767, stddev=1024, size=10240, seed=10) / Decompress",
    // "E2E / FieldLz16(clvl=3, dlvl=7) / Uniform16(card=100, size=10240,
    // seed=10) / Compress",
    "E2E / FieldLz16(clvl=3, dlvl=7) / Uniform16(card=100, size=10240, seed=10) / Decompress",
    "E2E / FieldLz32(clvl=0, dlvl=0) / Normal32(mean=2147483647, stddev=1024, size=10240, seed=10) / Decompress",
    // "E2E / FieldLz32(clvl=3, dlvl=1) / Normal32(mean=2147483647, stddev=1024,
    // size=10240, seed=10) / Compress",
    "E2E / FieldLz32(clvl=3, dlvl=1) / Normal32(mean=2147483647, stddev=1024, size=10240, seed=10) / Decompress",
    // "E2E / FieldLz32(clvl=3, dlvl=7) / Normal32(mean=2147483647, stddev=1024,
    // size=10240, seed=10) / Compress",
    "E2E / FieldLz32(clvl=3, dlvl=7) / Normal32(mean=2147483647, stddev=1024, size=10240, seed=10) / Decompress",
    "E2E / Huffman / Normal8(mean=128, stddev=10, size=10240, seed=10) / Compress",
    "E2E / Huffman / Normal8(mean=128, stddev=10, size=10240, seed=10) / Decompress",
    "E2E / Huffman / Uniform8(card=100, size=10240, seed=10) / Compress",
    "E2E / Huffman / Uniform8(card=100, size=10240, seed=10) / Decompress",
    "E2E / MergeSorted / SortedRuns32(numRuns=32, avgRunLength=1000, numUniqueValues=1600, seed=10) / Compress",
    "E2E / MergeSorted / SortedRuns32(numRuns=32, avgRunLength=1000, numUniqueValues=1600, seed=10) / Decompress",
    "E2E / Prefix / SortedVariable(nbBytes=1024, nbSegments=105, minSegLength=5, maxSegLength=15, alphabetSize=4) / Compress",
    "E2E / Prefix / SortedVariable(nbBytes=102400, nbSegments=6843, minSegLength=10, maxSegLength=20, alphabetSize=4) / Compress",
    "E2E / Prefix / SortedVariable(nbBytes=102400, nbSegments=6843, minSegLength=10, maxSegLength=20, alphabetSize=4) / Decompress",
    "E2E / SAO-Splitter / Uniform32(size=28000, seed=10) / Compress",
    "E2E / SAO-Splitter / Uniform32(size=28000, seed=10) / Decompress",
    "E2E / SAO-Splitter / Uniform32(size=280000, seed=10) / Decompress",
    "E2E / ThriftBinary_BigList<d>_TypeSplit / Arbitrary(size=1048581) / Compress",
    "E2E / ThriftBinary_BigList<i>_TypeSplit / Arbitrary(size=1048581) / Compress",
    "E2E / ThriftBinary_BigList<i>_TypeSplit / Arbitrary(size=1048581) / Decompress",
    "E2E / ThriftBinary_ManySmallLists<d>_TypeSplit / Arbitrary(size=1048589) / Compress",
    "E2E / ThriftBinary_ManySmallLists<d>_TypeSplit / Arbitrary(size=1048589) / Decompress",
    "E2E / ThriftBinary_ManySmallLists<i>_TypeSplit / Arbitrary(size=1048586) / Compress",
    "E2E / ThriftBinary_ManySmallLists<i>_TypeSplit / Arbitrary(size=1048586) / Decompress",
    "E2E / ThriftCompact_BigList<f>_TypeSplit / Arbitrary(size=1048580) / Compress",
    "E2E / ThriftCompact_BigList<f>_TypeSplit / Arbitrary(size=1048580) / Decompress",
    "E2E / ThriftCompact_BigList<i>_TypeSplit / Arbitrary(size=262148) / Compress",
    "E2E / ThriftCompact_BigList<i>_TypeSplit / Arbitrary(size=262148) / Decompress",
    "E2E / ThriftCompact_ManySmallLists<f>_TypeSplit / Arbitrary(size=726306) / Compress",
    "E2E / ThriftCompact_ManySmallLists<f>_TypeSplit / Arbitrary(size=726306) / Decompress",
    "E2E / ThriftCompact_ManySmallLists<l>_TypeSplit / Arbitrary(size=199735) / Compress",
    "E2E / ThriftCompact_ManySmallLists<l>_TypeSplit / Arbitrary(size=199735) / Decompress",
    "E2E / ThriftCompact_Random_TypeSplit / Arbitrary(size=886457) / Compress",
    "E2E / ThriftCompact_Random_TypeSplit / Arbitrary(size=886457) / Decompress",
    "E2E / Tokenize / Uniform16(card=100, size=1048576, seed=10) / Compress",
    "E2E / Tokenize / Uniform16(card=100, size=1048576, seed=10) / Decompress",
    "E2E / Tokenize / Uniform32(card=100, size=102400, seed=10) / Compress",
    "E2E / Tokenize / Uniform32(card=100, size=102400, seed=10) / Decompress",
    "E2E / TokenizeSorted / Uniform16(card=100, size=10240, seed=10) / Compress",
    "E2E / TokenizeSorted / Uniform16(card=100, size=10240, seed=10) / Decompress",
    "E2E / TokenizeSorted / Uniform32(card=100, size=102400, seed=10) / Compress",
    "E2E / TokenizeSorted / Uniform32(card=100, size=102400, seed=10) / Decompress",
    "E2E / TokenizeVSF / UnsortedVariable(nbBytes=10240, nbSegments=1875, minSegLength=1, maxSegLength=10, alphabetSize=4) / Compress",
    "E2E / TokenizeVSF / UnsortedVariable(nbBytes=10240, nbSegments=1875, minSegLength=1, maxSegLength=10, alphabetSize=4) / Decompress",
    "E2E / TokenizeVSF / UnsortedVariable(nbBytes=102400, nbSegments=18756, minSegLength=1, maxSegLength=10, alphabetSize=4) / Compress",
    "E2E / TokenizeVSF / UnsortedVariable(nbBytes=102400, nbSegments=18756, minSegLength=1, maxSegLength=10, alphabetSize=4) / Decompress",
    "E2E / TokenizeVSFSorted / UnsortedVariable(nbBytes=10240, nbSegments=1044, minSegLength=5, maxSegLength=15, alphabetSize=4) / Compress",
    "E2E / TokenizeVSFSorted / UnsortedVariable(nbBytes=10240, nbSegments=1875, minSegLength=1, maxSegLength=10, alphabetSize=4) / Decompress",
    "E2E / TransposeSplit / FixedSizeUniform(nbElts=1000, eltWidth=15) / Decompress",
    "E2E / TransposeSplit / FixedSizeUniform(nbElts=10000, eltWidth=4) / Compress",
    "E2E / TransposeSplit / FixedSizeUniform(nbElts=10000, eltWidth=4) / Decompress",
    "E2E / TransposeSplit / FixedSizeUniform(nbElts=100000, eltWidth=2) / Compress",
    "E2E / TransposeSplit / FixedSizeUniform(nbElts=100000, eltWidth=2) / Decompress",
    "E2E / TransposeSplit / FixedSizeUniform(nbElts=100000, eltWidth=8) / Compress",
    "E2E / TransposeSplit / FixedSizeUniform(nbElts=100000, eltWidth=8) / Decompress",
    "MCR / BrainFloat16 / Uniform16(card=100, size=10240, seed=10) / Decode",
    "MCR / BrainFloat16 / Uniform16(card=100, size=10240, seed=10) / Encode",
    "MCR / Float16 / Uniform16(card=100, size=10240, seed=10) / Decode",
    "MCR / Float16 / Uniform16(card=100, size=10240, seed=10) / Encode",
    "MCR / Float32 / Uniform32(card=100, size=10240, seed=10) / Decode",
    "MCR / Float32 / Uniform32(card=100, size=10240, seed=10) / Encode",
    "MCR / Float32 / Uniform32(card=100, size=10485760, seed=10) / Decode",
    "MCR / MicroPrefix / SortedVariable(nbBytes=10240, nbSegments=694, minSegLength=10, maxSegLength=20, alphabetSize=4) / Decode",
    "MCR / MicroPrefix / SortedVariable(nbBytes=10240, nbSegments=694, minSegLength=10, maxSegLength=20, alphabetSize=4) / Encode",
    "MCR / MicroTransposeSplit4 / FixedSizeUniform(nbElts=1024, eltWidth=4) / Encode",
    "MCR / MicroTransposeSplit4 / FixedSizeUniform(nbElts=10240, eltWidth=4) / Decode",
    "MCR / MicroTransposeSplit8 / FixedSizeUniform(nbElts=1024, eltWidth=8) / Decode",
    "MCR / MicroTransposeSplit8 / FixedSizeUniform(nbElts=1024, eltWidth=8) / Encode",
    "MCR / Varint32 / Uniform32(min=0, max=127, size=10240, seed=10) / Decode",
    "MCR / Varint32 / Uniform32(min=0, max=127, size=10240, seed=10) / Encode",
    "MCR / Varint32 / Uniform32(min=0, max=32768, size=10240, seed=10) / Decode",
    "MCR / Varint32 / Uniform32(min=0, max=32768, size=10240, seed=10) / Encode",
    "MCR / Varint64 / Uniform64(min=0, max=127, size=10240, seed=10) / Decode",
    "MCR / Varint64 / Uniform64(min=0, max=127, size=10240, seed=10) / Encode",
    "MCR / Varint64 / Uniform64(min=0, max=144115188075855872, size=10240, seed=10) / Decode",
    "MCR / Varint64 / Uniform64(min=0, max=144115188075855872, size=10240, seed=10) / Encode",
    "MCR / Varint64 / Uniform64(size=10240, seed=10) / Decode",
    "MCR / Varint64 / Uniform64(size=10240, seed=10) / Encode",
    "E2E / SAO / File(silesia/sao) / Compress",
    "E2E / SAO / File(silesia/sao) / Decompress",
    "E2E / BlocksSAO(blockSize=1008) / File(silesia/sao) / Compress",
    "E2E / BlocksSAO(blockSize=1008) / File(silesia/sao) / Decompress"
};
}

namespace zstrong::bench {

void BenchmarkConfig::setUseShortList(bool shortList)
{
    shortList_ = shortList;
}
bool BenchmarkConfig::useShortList() const
{
    return shortList_;
}
const std::vector<std::string>& BenchmarkConfig::getShortList() const
{
    return k_predefinedShortList; // @todo allow list to be modified
}
bool BenchmarkConfig::shouldRegister(std::string_view name) const
{
    if (useShortList()) {
        auto whitelist = getShortList();
        return std::find(whitelist.begin(), whitelist.end(), name)
                != whitelist.end();
    }
    return true;
}

BenchmarkConfig& BenchmarkConfig::instance()
{
    static BenchmarkConfig s;
    return s;
}

} // namespace zstrong::bench
