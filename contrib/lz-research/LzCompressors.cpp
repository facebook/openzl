// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "LzCompressors.hpp"

#include "codecs/Bucket.hpp"
#include "codecs/Bucket16.hpp"
#include "codecs/IsoByte.hpp"
#include "codecs/Lz.hpp"
#include "codecs/SmallInt.hpp"
#include "codecs/VarByte.hpp"
#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_partition.h"
#include "openzl/compress/private_nodes.h"

namespace openzl::lz {
namespace {
GraphID
smallIntGraph(Compressor& compressor, GraphID smallGraph, GraphID largeGraph)
{
    auto node = compressor.getNode("lz_research.small_int");
    if (!node.has_value()) {
        throw std::runtime_error("small_int node not found");
    }
    return compressor.buildStaticGraph(*node, { smallGraph, largeGraph });
}

GraphID
varByteGraph(Compressor& compressor, GraphID controlGraph, GraphID packedGraph)
{
    auto node = compressor.getNode("lz_research.var_byte");
    if (!node.has_value()) {
        throw std::runtime_error("var_byte node not found");
    }
    return compressor.buildStaticGraph(*node, { controlGraph, packedGraph });
}

GraphID isoByteGraph(
        Compressor& compressor,
        GraphID bitmapGraph   = ZL_GRAPH_HUFFMAN,
        GraphID highByteGraph = ZL_GRAPH_HUFFMAN)
{
    auto node = compressor.getNode("lz_research.iso_byte");
    if (!node.has_value()) {
        throw std::runtime_error("iso_byte node not found");
    }
    return compressor.buildStaticGraph(
            *node, { bitmapGraph, ZL_GRAPH_STORE, highByteGraph });
}

GraphID bucketGraph(Compressor& compressor, bool entropyCompressFixed = false)
{
    return BucketGraph::create(compressor, entropyCompressFixed);
}

GraphID bucket16Graph(Compressor& compressor)
{
    return Bucket16Graph::create(compressor);
}

std::string lzStoreCompressor(int llBits)
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto lzNode = registerLz(compressor, { .llBits = llBits });
    auto store  = graphs::Store{}();
    auto graph  = compressor.buildStaticGraph(
            lzNode,
            { graphs::ACE{ store }(compressor),
               graphs::ACE{ store }(compressor),
               graphs::ACE{ store }(compressor),
               graphs::ACE{ store }(compressor) });
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string lzLz4Compressor(int llBits, bool huf)
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto lzNode = registerLz(compressor, { .llBits = llBits });
    auto store  = graphs::Store{}();
    // TODO: Bitpack offsets when input is small enough
    auto lits  = huf ? graphs::Huffman{}() : store;
    auto graph = compressor.buildStaticGraph(
            lzNode,
            { lits,
              store,
              store,
              smallIntGraph(
                      compressor,
                      store,
                      nodes::RangePack{}(compressor, store)) });
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string
lzZstdCompressor(int llBits, bool slow, bool iso = false, bool bucket = false)
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto lzNode = registerLz(compressor, { .llBits = llBits });
    auto store  = graphs::Store{}();
    auto quantize =
            nodes::QuantizeLengths{}(compressor, graphs::Fse{}(), store);
    auto huf     = graphs::Huffman{}();
    auto lits    = huf;
    auto tokens  = huf;
    auto offsets = slow
            ? (iso ? isoByteGraph(compressor, huf, bucketGraph(compressor))
                   : varByteGraph(compressor, huf, store))
            : store;
    if (bucket) {
        offsets = bucket16Graph(compressor);
    }
    offsets        = ZL_GRAPH_PARTITION_BITPACK;
    auto extraLens = smallIntGraph(
            compressor,
            slow ? huf : store,
            sizeof(length_t) == 2 ? store : quantize);
    auto graph = compressor.buildStaticGraph(
            lzNode, { lits, tokens, offsets, extraLens });
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string lzStdCompressor(bool u16Offset)
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    openzl::LocalParams params;
    params.addIntParam(ZL_LzParam_windowLog, u16Offset ? 16 : 32);
    auto graph = compressor.parameterizeGraph(
            ZL_GRAPH_LZ,
            openzl::GraphParameters{ .localParams = std::move(params) });
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string varByteCompressor(int width)
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto store = graphs::Store{}();
    auto huf   = graphs::Huffman{}();
    auto graph = varByteGraph(compressor, huf, store);
    graph      = nodes::ConvertSerialToNumLE{ width }(compressor, graph);
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string isoByteCompressor(bool useBucket, bool entropy = false)
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto huf   = graphs::Huffman{}();
    auto graph = isoByteGraph(
            compressor,
            huf,
            useBucket ? bucketGraph(compressor, entropy) : huf);
    graph = nodes::ConvertSerialToNumLE{ 2 }(compressor, graph);
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string smallIntCompressor(int width, bool slow)
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto store = graphs::Store{}();
    auto huf   = graphs::Huffman{}();
    auto quantize =
            nodes::QuantizeLengths{}(compressor, graphs::Fse{}(), store);
    auto graph = smallIntGraph(
            compressor,
            slow ? huf : store,
            width == 2 ? ZL_GRAPH_PARTITION_BITPACK : quantize);
    graph = nodes::ConvertSerialToNumLE{ width }(compressor, graph);
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string bucket16Compressor()
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto graph = bucket16Graph(compressor);
    graph      = nodes::ConvertSerialToNumLE{ 2 }(compressor, graph);
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string partitionOffsetsCompressor()
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto graph = ZL_GRAPH_PARTITION_BITPACK;
    graph      = nodes::ConvertSerialToNumLE{ 2 }(compressor, graph);
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

std::string huff16Compressor()
{
    openzl::Compressor compressor;
    registerGraphComponents(compressor);
    auto graph = ZL_GRAPH_HUFFMAN;
    graph      = nodes::ConvertSerialToStruct{ 2 }(compressor, graph);
    compressor.selectStartingGraph(graph);
    return compressor.serialize();
}

} // namespace

std::string getSerializedCompressor(std::string_view name)
{
    for (int llBits = 0; llBits <= 7; ++llBits) {
        auto suffix = "[llbits=" + std::to_string(llBits) + "]";
        if (llBits == 0) {
            suffix = "";
        }
        if (name == "lz-store" + suffix) {
            return lzStoreCompressor(llBits);
        } else if (name == "lz-lz4" + suffix) {
            return lzLz4Compressor(llBits, false);
        } else if (name == "lz-lz4-huf" + suffix) {
            return lzLz4Compressor(llBits, true);
        } else if (name == "lz-zstd-fast" + suffix) {
            return lzZstdCompressor(llBits, false);
        } else if (name == "lz-zstd-slow" + suffix) {
            return lzZstdCompressor(llBits, true);
        } else if (name == "lz-zstd-iso" + suffix) {
            return lzZstdCompressor(llBits, true, true);
        } else if (name == "lz-zstd-buc" + suffix) {
            return lzZstdCompressor(llBits, false, false, true);
        }
    }
    if (name == "varbyte16") {
        return varByteCompressor(2);
    } else if (name == "isobyte16") {
        return isoByteCompressor(false);
    } else if (name == "isobyte16-bucket") {
        return isoByteCompressor(true);
    } else if (name == "isobyte16-bucket-huf") {
        return isoByteCompressor(true, true);
    } else if (name == "varbyte32") {
        return varByteCompressor(4);
    } else if (name == "smallint16-store") {
        return smallIntCompressor(2, false);
    } else if (name == "smallint16-huf") {
        return smallIntCompressor(2, true);
    } else if (name == "smallint32-store") {
        return smallIntCompressor(4, false);
    } else if (name == "smallint32-huf") {
        return smallIntCompressor(4, true);
    } else if (name == "bucket16") {
        return bucket16Compressor();
    } else if (name == "partition-offsets") {
        return partitionOffsetsCompressor();
    } else if (name == "lz-std-16") {
        return lzStdCompressor(true);
    } else if (name == "lz-std-32") {
        return lzStdCompressor(false);
    } else if (name == "huff16") {
        return huff16Compressor();
    }
    throw std::runtime_error("Unknown compressor: " + std::string(name));
}

void registerGraphComponents(openzl::Compressor& compressor)
{
    compressor.registerFunctionGraph(std::make_shared<BucketGraph>());
    compressor.registerFunctionGraph(std::make_shared<Bucket16Graph>());
    registerLz(compressor, {});
    compressor.registerCustomEncoder(std::make_shared<SmallIntEncoder>());
    compressor.registerCustomEncoder(std::make_shared<VarByteEncoder>());
    compressor.registerCustomEncoder(std::make_shared<IsoByteEncoder>());
    compressor.registerCustomEncoder(std::make_shared<BucketEncoder>());
    compressor.registerCustomEncoder(std::make_shared<Bucket16Encoder>());
}

void registerCustomCodecs(openzl::DCtx& dctx)
{
    registerLz(dctx);
    dctx.registerCustomDecoder(std::make_shared<SmallIntDecoder>());
    dctx.registerCustomDecoder(std::make_shared<VarByteDecoder>());
    dctx.registerCustomDecoder(std::make_shared<IsoByteDecoder>());
    dctx.registerCustomDecoder(std::make_shared<BucketDecoder>());
    dctx.registerCustomDecoder(std::make_shared<Bucket16Decoder>());
}

} // namespace openzl::lz
