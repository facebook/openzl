// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/utils/compress_profiles.h"
#include "cli/utils/util.h"

#include <string.h>

#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_mlselector.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/openzl.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_reflection.h"

#include "custom_parsers/csv/csv_profile.h"
#include "custom_parsers/dependency_registration.h"
#include "custom_parsers/parquet/parquet_graph.h"
#include "custom_parsers/pytorch_model_parser.h"
#include "custom_parsers/sddl/sddl2_profile.h"
#include "custom_parsers/sddl/sddl_profile.h"
#include "custom_parsers/shared_components/clustering.h"

#include "tools/io/InputFile.h"
#include "tools/io/InputSetFileOrDir.h"
#include "tools/sddl/compiler/Compiler.h"

namespace openzl::cli {
namespace {
ZL_GraphID saoProfile(Compressor& compressor)
{
    compressor.setParameter(CParam::CompressionLevel, 1);
    /* The SAO format consists of a header,
     * which is 28 bytes for the dirSilesia/sao sample specifically,
     * followed by an array of structures, each one describing a star.
     *
     * For the record, here is the Header format (it's currently ignored):
     *
     * Integer*4 STAR0=0   Subtract from star number to get sequence number
     * Integer*4 STAR1=1   First star number in file
     * Integer*4 STARN=258996  Number of stars in file (pos 8)
     * Integer*4 STNUM=1   0 if no star i.d. numbers are present
     *                     1 if star i.d. numbers are in catalog file
     *                     2 if star i.d. numbers are  in file
     * Logical*4 MPROP=t   True if proper motion is included
     *                     False if no proper motion is included
     * Integer*4 NMAG=1    Number of magnitudes present
     * Integer*4 NBENT=32  Number of bytes per star entry
     * Total : 28 bytes
     */
    size_t const headerSize = 28;

    /* Star record : 28 bytes for the dirSilesia/sao sample specifically
     * Real*4 XNO       Catalog number of star (not present, since stnum==0)
     * Real*8 SRA0      B1950 Right Ascension (radians)
     * Real*8 SDEC0     B1950 Declination (radians)
     * Character*2 IS   Spectral type (2 characters)
     * Integer*2 MAG    V Magnitude * 100
     * Real*4 XRPM      R.A. proper motion (radians per year)
     * Real*4 XDPM      Dec. proper motion (radians per year)
     */
    ZL_GraphID sra0 = graphs::ACE(
            nodes::ConvertStructToNumLE()(
                    compressor,
                    nodes::DeltaInt()(
                            compressor, graphs::FieldLz()(compressor))))(
            compressor);
    ZL_GraphID sdec0 = graphs::ACE(
            nodes::TransposeSplit()(compressor, graphs::Zstd()(compressor)))(
            compressor);
    ZL_GraphID token_compress = nodes::TokenizeStruct()(
            compressor,
            graphs::FieldLz()(compressor),
            graphs::FieldLz()(compressor));
    ZL_GraphID num_huffman = nodes::ConvertStructToNumLE()(
            compressor,
            nodes::TokenizeNumeric(/* sort */ false)(
                    compressor,
                    graphs::Huffman()(compressor),
                    graphs::Huffman()(compressor)));

    ZL_GraphID is   = graphs::ACE(num_huffman)(compressor);
    ZL_GraphID mag  = graphs::ACE(num_huffman)(compressor);
    ZL_GraphID xrpm = graphs::ACE(token_compress)(compressor);
    ZL_GraphID xdpm = graphs::ACE(token_compress)(compressor);

    const std::array<size_t, 6> fieldSizes      = { 8, 8, 2, 2, 4, 4 };
    const std::array<ZL_GraphID, 6> fieldGraphs = { sra0, sdec0, is,
                                                    mag,  xrpm,  xdpm };

    ZL_GraphID splitStructure = ZL_Compressor_registerSplitByStructGraph(
            compressor.get(),
            fieldSizes.data(),
            fieldGraphs.data(),
            fieldSizes.size());

    const std::array<size_t, 2> splitSizes      = { headerSize, 0 };
    const std::array<ZL_GraphID, 2> splitGraphs = { ZL_GRAPH_STORE,
                                                    splitStructure };

    return ZL_Compressor_registerSplitGraph(
            compressor.get(),
            ZL_Type_serial,
            splitSizes.data(),
            splitGraphs.data(),
            splitSizes.size());
}

static std::string makeProfileName(const std::string& signage, size_t bitWidth)
{
    if (bitWidth == 8) {
        return signage + "8";
    }
    return "le-" + signage + std::to_string(bitWidth);
}

static std::string makeProfileDescription(bool isSigned, size_t bitWidth)
{
    if (bitWidth == 8) {
        return (isSigned ? "Signed " : "Unsigned ") + std::string("8-bit data");
    }
    return std::string("Little-endian ") + (isSigned ? "signed " : "unsigned ")
            + std::to_string(bitWidth) + "-bit data";
}

static void addIntProfile(
        std::map<std::string, std::shared_ptr<CompressProfile>>& mp,
        bool isSigned,
        size_t bitWidth)
{
    std::string signage     = isSigned ? "i" : "u";
    std::string name        = makeProfileName(signage, bitWidth);
    std::string description = makeProfileDescription(isSigned, bitWidth);

    auto interpretAsLEnode = ZL_Node_interpretAsLE(bitWidth);

    std::shared_ptr<void> nodeid = std::shared_ptr<void>(
            malloc(2 * sizeof(interpretAsLEnode)), [](void* p) { free(p); });
    ((ZL_NodeID*)nodeid.get())[0] = interpretAsLEnode;
    ((ZL_NodeID*)nodeid.get())[1] = ZL_NODE_ZIGZAG;

    mp[name] = std::make_shared<CompressProfile>(
            name,
            description,
            isSigned ? ([](ZL_Compressor* comp,
                           void* opaque,
                           const ProfileArgs&) {
                const ZL_NodeID* nodes = (const ZL_NodeID*)opaque;
                auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                        comp, nodes[1], ZL_GRAPH_FIELD_LZ);
                graph = ZL_Compressor_buildACEGraphWithDefault(comp, graph);
                return ZL_Compressor_registerStaticGraph_fromNode1o(
                        comp, nodes[0], graph);
            })
                     : ([](ZL_Compressor* comp,
                           void* opaque,
                           const ProfileArgs&) {
                           const ZL_NodeID* nodes = (const ZL_NodeID*)opaque;
                           auto graph = ZL_Compressor_buildACEGraphWithDefault(
                                   comp, ZL_GRAPH_FIELD_LZ);
                           return ZL_Compressor_registerStaticGraph_fromNode1o(
                                   comp, nodes[0], graph);
                       }),
            std::move(nodeid));
}
} // namespace

static ZL_RESULT_OF(ZL_GraphID) extractFolderOfCompressors(
        Compressor& compressor,
        const std::string& folder)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor.get());

    auto inputSet = tools::io::InputSetFileOrDir(folder, true);

    std::vector<ZL_GraphID> successors;
    for (const auto& input : inputSet) {
        auto contents = input->contents();

        const auto deps = compressor.getUnmetDependencies(contents);

        for (const auto& graphName : deps.graphNames) {
            if (graphName == "Parquet Parser" || graphName == "CSV Parser") {
                throw std::runtime_error(
                        "CSV and Parquet parsers are not supported in ML Selector profiles. "
                        "CSV and Parquet parsers require clustering which ML Selector does not provide.");
            }
        }

        compressor.deserialize(contents);

        ZL_GraphID startingGraphId;
        ZL_Compressor_getStartingGraphID(compressor.get(), &startingGraphId);
        successors.push_back(startingGraphId);
    }

    // ML selectors require at least 2 successors to choose between
    if (successors.size() < 2) {
        throw Exception(
                "ML selector requires at least 2 successor compressors, but "
                "only "
                + std::to_string(successors.size()) + " were provided in '"
                + folder + "'");
    }

    ZL_TRY_LET(
            ZL_GraphID,
            mlSelectorGraphId,
            ZL_Compressor_buildUntrainedMLSelector(
                    compressor.get(), successors.data(), successors.size()));

    // Wrap with serial-to-numeric conversion so the graph accepts serial input
    ZL_GraphID staticGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor.get(),
            ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64,
            mlSelectorGraphId);

    // Parameterize so ml selector graph can be updated during training
    ZL_GraphParameters const wrapperDesc = {};

    return ZL_Compressor_parameterizeGraph(
            compressor.get(), staticGraph, &wrapperDesc);
}

/**
 * @brief Registers static successor graphs for the ML selector.
 * NOTE: This is a temporary placeholder
 * @param compressor The compressor to register the graph with
 */
static ZL_RESULT_OF(ZL_GraphID)
        numeric64BitMLSelectorProfile(ZL_Compressor* compressor)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);

    ZL_GraphID fieldlz    = ZL_Compressor_registerFieldLZGraph(compressor);
    ZL_GraphID range_pack = ZL_Compressor_registerStaticGraph_fromNode(
            compressor, ZL_NODE_RANGE_PACK, &fieldlz, 1);
    ZL_GraphID zstd            = ZL_GRAPH_ZSTD;
    ZL_GraphID range_pack_zstd = ZL_Compressor_registerStaticGraph_fromNode(
            compressor, ZL_NODE_RANGE_PACK, &zstd, 1);
    ZL_GraphID delta_fieldlz = ZL_Compressor_registerStaticGraph_fromNode(
            compressor, ZL_NODE_DELTA_INT, &fieldlz, 1);
    ZL_GraphID tokenize_delta_fieldlz = ZL_Compressor_registerTokenizeGraph(
            compressor,
            ZL_Type_numeric,
            /* sort */ true,
            delta_fieldlz,
            fieldlz);

    ZL_GraphID successors[6] = { fieldlz,
                                 range_pack,
                                 range_pack_zstd,
                                 delta_fieldlz,
                                 tokenize_delta_fieldlz,
                                 zstd };

    ZL_TRY_LET(
            ZL_GraphID,
            mlSelectorGraphId,
            ZL_Compressor_buildUntrainedMLSelector(compressor, successors, 6));

    // Wrap with serial-to-numeric conversion so the graph accepts serial input
    ZL_GraphID staticGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64, mlSelectorGraphId);

    // Parameterize so ml selector graph can be updated during training
    ZL_GraphParameters const wrapperDesc = {};

    return ZL_Compressor_parameterizeGraph(
            compressor, staticGraph, &wrapperDesc);
}

const std::map<std::string, std::shared_ptr<CompressProfile>>&
compressProfiles()
{
    static const std::map<std::string, std::shared_ptr<CompressProfile>> staticProfiles = []() {
        std::map<std::string, std::shared_ptr<CompressProfile>> mp;

        std::string kSerialName = "serial";
        mp[kSerialName]         = std::make_shared<CompressProfile>(
                kSerialName,
                "Serial data (aka raw bytes)",
                [](ZL_Compressor* compressor, void*, const ProfileArgs&) {
                    return ZL_Compressor_buildACEGraphWithDefault(
                            compressor, ZL_GRAPH_ZSTD);
                });

        std::string kPytorchName = "pytorch";
        mp[kPytorchName]         = std::make_shared<CompressProfile>(
                kPytorchName,
                "Pytorch model generated from torch.save(). Training is not supported.",
                [](ZL_Compressor* comp, void*, const ProfileArgs&) {
                    return ZS2_createGraph_pytorchModelCompressor(comp);
                });

        std::string kCsvName = "csv";
        mp[kCsvName]         = std::make_shared<CompressProfile>(
                kCsvName,
                "CSV. Pass optional non-comma separator with --profile-arg <char>.",
                [](ZL_Compressor* comp, void*, const ProfileArgs& args) {
                    char sep             = ',';
                    const auto chunkSize = args.chunkSize().value_or(
                            custom_parsers::kDefaultChunkSize);
                    auto argmap = args.map();
                    auto it     = argmap.find("TBD");
                    if (it != argmap.end()) {
                        auto str = it->second;
                        if (str.size() != 1) {
                            throw InvalidArgsException(
                                    "The CSV profile separator must be a single character. Pass it with --profile-arg <char>.");
                        }
                        sep = str[0];
                    }
                    return openzl::custom_parsers::
                            ZL_createGraph_genericCSVCompressorWithOptions(
                                    comp, chunkSize, true, sep, false);
                });

        addIntProfile(mp, true, 8);
        addIntProfile(mp, false, 8);
        addIntProfile(mp, true, 16);
        addIntProfile(mp, false, 16);
        addIntProfile(mp, true, 32);
        addIntProfile(mp, false, 32);
        addIntProfile(mp, true, 64);
        addIntProfile(mp, false, 64);

        std::string kParquetName = "parquet";
        mp[kParquetName]         = std::make_shared<CompressProfile>(
                kParquetName,
                "Parquet in the canonical format (no compression, plain encoding)",
                [](ZL_Compressor* comp, void*, const ProfileArgs&) {
                    auto clustering = ZS2_createGraph_genericClustering(comp);
                    return ZL_Parquet_registerGraph_withChunkSize(
                            comp,
                            clustering,
                            custom_parsers::kDefaultChunkSize);
                });

        std::string kSDDLName = "sddl";
        mp[kSDDLName]         = std::make_shared<CompressProfile>(
                kSDDLName,
                "Data that can be parsed using the Simple Data Description Language. Pass a path to the data description file with --profile-arg.",
                [](ZL_Compressor* comp, void*, const ProfileArgs& args) {
                    auto argmap = args.map();
                    auto it     = argmap.find("TBD");
                    if (it == argmap.end()) {
                        throw InvalidArgsException(
                                "The Simple Data Description Language profile requires a data description. Pass a path to the description file with --profile-arg.");
                    }
                    auto progInput = tools::io::InputFile(it->second);
                    auto compiled  = sddl::Compiler{}.compile(
                            progInput.contents(), progInput.name());
                    return unwrap(
                            ZL_SDDL_setupProfile(
                                    comp, compiled.data(), compiled.size()),
                            "Failed to set up SDDL profile",
                            comp);
                });

        std::string kSDDL2Name = "sddl2";
        mp[kSDDL2Name]         = std::make_shared<CompressProfile>(
                kSDDL2Name,
                "Data that can be parsed using Simple Data Description Language v2. Pass a path to the pre-compiled bytecode file with --profile-arg.",
                [](ZL_Compressor* comp, void*, const ProfileArgs& args) {
                    auto argmap = args.map();
                    auto it     = argmap.find("TBD");
                    if (it == argmap.end()) {
                        throw InvalidArgsException(
                                "The Simple Data Description Language v2 profile requires pre-compiled bytecode. Pass a path to the bytecode file with --profile-arg.");
                    }
                    auto bytecodeInput = tools::io::InputFile(it->second);
                    auto bytecode      = bytecodeInput.contents();
                    return unwrap(
                            ZL_SDDL2_setupProfile(
                                    comp, bytecode.data(), bytecode.size()),
                            "Failed to set up SDDL2 profile",
                            comp);
                });

        std::string kSAOName = "sao";
        mp[kSAOName]         = std::make_shared<CompressProfile>(
                kSAOName,
                "SAO format from the Silesia corpus",
                [](ZL_Compressor* comp, void*, const ProfileArgs&) {
                    CompressorRef compressor(comp);
                    return saoProfile(compressor);
                });

        std::string kGenericNumericName = "numeric-ml-selector-64";
        mp[kGenericNumericName]         = std::make_shared<CompressProfile>(
                kGenericNumericName,
                "64 bit numeric data using ml selectors (Placeholder)",
                [](ZL_Compressor* comp, void*, const ProfileArgs& args) {
                    (void)args;
                    if (args.map().find("TBD") != args.map().end()) {
                        CompressorRef compressor(comp);
                        return unwrap(extractFolderOfCompressors(
                                compressor, args.map().at("TBD")));
                    } else {
                        return unwrap(
                                numeric64BitMLSelectorProfile(comp),
                                "Failed to set up numeric profile",
                                comp);
                    }
                });

        return mp;
    }();
    return staticProfiles;
};

} // namespace openzl::cli
