// SPDX-License-Identifier: BSD-3-Clause
// Nasdaq BinaryFILE parser built entirely from stock OpenZL nodes.
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <vector>
#include "openzl/codecs/zl_brute_force_selector.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_delta.h"
#include "openzl/codecs/zl_dispatch.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_split_by_struct.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_graph_api.h"

namespace {
struct Field {
    size_t offset, width;
};
struct Schema {
    unsigned char type;
    size_t size;
    std::vector<Field> extra;
};
const std::vector<Schema>& schemas()
{
    static const std::vector<Schema> s = {
        { 'S', 12, {} },
        { 'R', 39, { { 21, 4 }, { 34, 4 } } },
        { 'H', 25, {} },
        { 'Y', 20, {} },
        { 'L', 26, {} },
        { 'V', 35, { { 11, 8 }, { 19, 8 }, { 27, 8 } } },
        { 'W', 12, {} },
        { 'K', 28, { { 19, 4 }, { 24, 4 } } },
        { 'J', 35, { { 19, 4 }, { 23, 4 }, { 27, 4 }, { 31, 4 } } },
        { 'h', 21, {} },
        { 'A', 36, { { 11, 8 }, { 20, 4 }, { 32, 4 } } },
        { 'F', 40, { { 11, 8 }, { 20, 4 }, { 32, 4 } } },
        { 'E', 31, { { 11, 8 }, { 19, 4 }, { 23, 8 } } },
        { 'C', 36, { { 11, 8 }, { 19, 4 }, { 23, 8 }, { 32, 4 } } },
        { 'X', 23, { { 11, 8 }, { 19, 4 } } },
        { 'D', 19, { { 11, 8 } } },
        { 'U', 35, { { 11, 8 }, { 19, 8 }, { 27, 4 }, { 31, 4 } } },
        { 'P', 44, { { 11, 8 }, { 20, 4 }, { 32, 4 }, { 36, 8 } } },
        { 'Q', 40, { { 11, 8 }, { 27, 4 }, { 31, 8 } } },
        { 'B', 19, { { 11, 8 } } },
        { 'I', 50, { { 11, 8 }, { 19, 8 }, { 36, 4 }, { 40, 4 }, { 44, 4 } } },
        { 'N', 20, {} }
    };
    return s;
}

ZL_Report parse(ZL_Graph* graph, ZL_Edge* inputs[], size_t n) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_NE(n, 1, graphParameter_invalid);
    const auto* input = ZL_Edge_getData(inputs[0]);
    const auto* bytes = static_cast<const uint8_t*>(ZL_Input_ptr(input));
    const size_t size = ZL_Input_numElts(input);
    if (!size)
        return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_COMPRESS_GENERIC);
    try {
        const auto& spec  = schemas();
        auto destinations = ZL_Graph_getCustomGraphs(graph);
        ZL_ERR_IF_NE(
                destinations.nbGraphIDs,
                spec.size() + 1,
                graphParameter_invalid);
        std::array<int, 256> byType;
        byType.fill(-1);
        for (size_t i = 0; i < spec.size(); ++i)
            byType[spec[i].type] = static_cast<int>(i);
        std::vector<size_t> sizes;
        std::vector<unsigned> tags, groupSchema;
        std::vector<int> groups(spec.size() + 1, -1);
        for (size_t pos = 0; pos < size;) {
            ZL_ERR_IF_LT(size - pos, 2, srcSize_tooSmall);
            const size_t len =
                    (static_cast<size_t>(bytes[pos]) << 8) | bytes[pos + 1];
            ZL_ERR_IF_LT(size - pos - 2, len, srcSize_tooSmall);
            int index = len ? byType[bytes[pos + 2]] : -1;
            if (index < 0 || spec[static_cast<size_t>(index)].size != len)
                index = static_cast<int>(spec.size());
            if (groups[index] < 0) {
                groups[index] = static_cast<int>(groupSchema.size());
                groupSchema.push_back(static_cast<unsigned>(index));
            }
            sizes.push_back(len + 2);
            tags.push_back(static_cast<unsigned>(groups[index]));
            pos += len + 2;
        }
        ZL_DispatchInstructions instructions{};
        instructions.segmentSizes = sizes.data();
        instructions.tags         = tags.data();
        instructions.nbSegments   = sizes.size();
        instructions.nbTags       = static_cast<unsigned>(groupSchema.size());
        ZL_TRY_LET(
                ZL_EdgeList,
                output,
                ZL_Edge_runDispatchNode(inputs[0], &instructions));
        ZL_ERR_IF_NE(
                output.nbEdges, groupSchema.size() + 2, graphParameter_invalid);
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                output.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                output.edges[1], ZL_GRAPH_COMPRESS_GENERIC));
        for (size_t i = 0; i < groupSchema.size(); ++i)
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                    output.edges[i + 2],
                    destinations.graphids[groupSchema[i]]));
        return ZL_returnSuccess();
    } catch (const std::bad_alloc&) {
        ZL_ERR(allocation);
    } catch (...) {
        ZL_ERR(GENERIC);
    }
}
} // namespace

extern "C" ZL_GraphID registerItchParser(ZL_Compressor* compressor)
{
    // Integer predictors are generic, with no order-book or symbol state.
    ZL_GraphID choices[] = {
        ZL_GRAPH_NUMERIC,
        ZL_GRAPH_FIELD_LZ,
        ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor, ZL_NODE_DELTA_INT, ZL_GRAPH_NUMERIC),
        ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor, ZL_NODE_DELTA_INT, ZL_GRAPH_FIELD_LZ)
    };
    auto selector =
            ZL_Compressor_buildBruteForceSelectorGraph(compressor, choices, 4);
    if (ZL_RES_isError(selector))
        return ZL_GRAPH_ILLEGAL;
    ZL_GraphID number = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor,
            ZL_NODE_CONVERT_STRUCT_TO_NUM_BE,
            ZL_RES_value(selector));
    std::vector<ZL_GraphID> graphs;
    for (const auto& s : schemas()) {
        // Prefix, type, locate, tracking, timestamp high16 and low32.
        // Splitting the 48-bit timestamp avoids a custom widening transform.
        std::vector<size_t> widths   = { 2, 1, 2, 2, 2, 4 };
        std::vector<ZL_GraphID> next = { number, number, number,
                                         number, number, number };
        size_t offset                = 11;
        for (const auto& field : s.extra) {
            if (offset < field.offset) {
                widths.push_back(field.offset - offset);
                next.push_back(ZL_GRAPH_COMPRESS_GENERIC);
            }
            widths.push_back(field.width);
            next.push_back(number);
            offset = field.offset + field.width;
        }
        if (offset < s.size) {
            widths.push_back(s.size - offset);
            next.push_back(ZL_GRAPH_COMPRESS_GENERIC);
        }
        graphs.push_back(ZL_Compressor_registerSplitByStructGraph(
                compressor, widths.data(), next.data(), widths.size()));
    }
    graphs.push_back(ZL_GRAPH_COMPRESS_GENERIC); // all unknown/noncanonical
                                                 // complete records
    ZL_Type type = ZL_Type_serial;
    ZL_FunctionGraphDesc descriptor{};
    descriptor.name           = "Nasdaq BinaryFILE ITCH";
    descriptor.graph_f        = parse;
    descriptor.inputTypeMasks = &type;
    descriptor.nbInputs       = 1;
    descriptor.customGraphs   = graphs.data();
    descriptor.nbCustomGraphs = graphs.size();
    return ZL_Compressor_registerFunctionGraph(compressor, &descriptor);
}

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(ZL_Report result)
{
    if (ZL_isError(result)) {
        throw std::runtime_error("OpenZL operation failed");
    }
}

std::vector<uint8_t> compressItch(const std::vector<uint8_t>& input)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    ZL_CCtx* context          = ZL_CCtx_create();
    if (!compressor || !context) {
        ZL_Compressor_free(compressor);
        ZL_CCtx_free(context);
        throw std::bad_alloc();
    }
    try {
        check(ZL_Compressor_selectStartingGraphID(
                compressor, registerItchParser(compressor)));
        check(ZL_CCtx_refCompressor(context, compressor));
        check(ZL_CCtx_setParameter(context, ZL_CParam_compressionLevel, 9));
        check(ZL_CCtx_setParameter(
                context, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        check(ZL_CCtx_setParameter(context, ZL_CParam_compressedChecksum, 1));
        check(ZL_CCtx_setParameter(context, ZL_CParam_contentChecksum, 1));
        std::vector<uint8_t> output(ZL_compressBound(input.size()));
        auto result = ZL_CCtx_compress(
                context,
                output.data(),
                output.size(),
                input.data(),
                input.size());
        if (ZL_isError(result)) {
            throw std::runtime_error(
                    ZL_CCtx_getErrorContextString(context, result));
        }
        output.resize(ZL_validResult(result));
        ZL_CCtx_free(context);
        ZL_Compressor_free(compressor);
        return output;
    } catch (...) {
        ZL_CCtx_free(context);
        ZL_Compressor_free(compressor);
        throw;
    }
}

void roundTrip(const std::vector<uint8_t>& original)
{
    auto encoded = compressItch(original);
    std::vector<uint8_t> decoded(original.size());
    // No ITCH decoder, parser registration, or custom transforms are used here.
    auto result = ZL_decompress(
            decoded.data(), decoded.size(), encoded.data(), encoded.size());
    check(result);
    if (ZL_validResult(result) != original.size() || decoded != original) {
        throw std::runtime_error("round trip differs");
    }
    encoded[encoded.size() / 2] ^= 1;
    if (!ZL_isError(ZL_decompress(
                decoded.data(),
                decoded.size(),
                encoded.data(),
                encoded.size()))) {
        throw std::runtime_error("altered compressed frame was accepted");
    }
}

void selfTest()
{
    roundTrip({});
    std::vector<uint8_t> data;
    for (unsigned repeat = 0; repeat < 8; ++repeat) {
        for (const auto& s : schemas()) {
            for (unsigned value : { 0U, 255U }) {
                data.push_back(static_cast<uint8_t>(s.size >> 8));
                data.push_back(static_cast<uint8_t>(s.size));
                data.push_back(s.type);
                data.insert(
                        data.end(), s.size - 1, static_cast<uint8_t>(value));
            }
        }
        // Unknown type, zero-length record, and a known type at another length.
        const uint8_t fallback[] = { 0, 2, '?', '!', 0, 0, 0, 3, 'A', 0, 255 };
        data.insert(data.end(), std::begin(fallback), std::end(fallback));
    }
    roundTrip(data);
    // A long sequence with changing prices/IDs/timestamps exercises predictors.
    data.clear();
    for (unsigned i = 0; i < 4096; ++i) {
        data.push_back(0);
        data.push_back(36);
        data.push_back('A');
        for (unsigned j = 1; j < 36; ++j) {
            data.push_back(static_cast<uint8_t>((i >> (j % 4)) + j));
        }
    }
    roundTrip(data);
    std::cout
            << "ITCH example: stock decoder round trips and corruption controls pass\n";
}
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) {
            selfTest();
            return 0;
        }
        if (argc != 3) {
            std::cerr
                    << "Usage: " << argv[0] << " INPUT.itch OUTPUT.zl\n       "
                    << argv[0]
                    << " --self-test\nInput must contain complete BinaryFILE records (max 64 MiB).\n";
            return 2;
        }
        std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
        if (!in || in.tellg() < 0 || in.tellg() > 64 * 1024 * 1024) {
            throw std::runtime_error("input cannot be read or exceeds 64 MiB");
        }
        const auto size = static_cast<size_t>(in.tellg());
        in.seekg(0);
        std::vector<uint8_t> data(size);
        if (!in.read(reinterpret_cast<char*>(data.data()), size)) {
            throw std::runtime_error("input read failed");
        }
        for (size_t pos = 0; pos < size;) {
            if (size - pos < 2)
                throw std::runtime_error("short record prefix");
            const size_t len =
                    (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
            if (len > size - pos - 2)
                throw std::runtime_error("short record body");
            pos += len + 2;
        }
        if (std::ifstream(argv[2], std::ios::binary).good()) {
            throw std::runtime_error("output already exists");
        }
        const auto output = compressItch(data);
        std::ofstream out(argv[2], std::ios::binary);
        if (!out
            || !out.write(
                    reinterpret_cast<const char*>(output.data()),
                    output.size())) {
            throw std::runtime_error("output write failed");
        }
        out.close();
        if (!out)
            throw std::runtime_error("output close failed");
        std::cerr << data.size() << " -> " << output.size() << " bytes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
