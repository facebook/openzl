// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/utils/compress_profiles.h"
#include "cli/utils/util.h"

#include <string.h>

#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_sddl.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/openzl.hpp"
#include "openzl/zl_compressor.h"

#include "custom_parsers/csv/csv_profile.h"
#include "custom_parsers/parquet/parquet_graph.h"
#include "custom_parsers/pytorch_model_parser.h"
#include "custom_parsers/sddl/sddl_profile.h"
#include "custom_parsers/shared_components/clustering.h"

#include "tools/io/InputFile.h"
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

static void addLEintProfile(
        std::map<std::string, std::shared_ptr<CompressProfile>>& mp,
        bool isSigned,
        size_t bitWidth)
{
    std::string signage    = isSigned ? "i" : "u";
    std::string name       = "le-" + signage + std::to_string(bitWidth);
    auto interpretAsLEnode = ZL_Node_interpretAsLE(bitWidth);

    std::shared_ptr<void> nodeid = std::shared_ptr<void>(
            malloc(2 * sizeof(interpretAsLEnode)), [](void* p) { free(p); });
    ((ZL_NodeID*)nodeid.get())[0] = interpretAsLEnode;
    ((ZL_NodeID*)nodeid.get())[1] = ZL_NODE_ZIGZAG;

    mp[name] = std::make_shared<CompressProfile>(
            name,
            std::string("Little-endian ") + (isSigned ? "signed " : "unsigned ")
                    + std::to_string(bitWidth) + "-bit data",
            isSigned ? ([](ZL_Compressor* comp,
                           void* opaque,
                           const ProfileArgs&) {
                auto graph =
                        ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
                                comp, (ZL_NodeID*)opaque, 2, ZL_GRAPH_FIELD_LZ);
                return ZL_Compressor_buildACEGraphWithDefault(comp, graph);
            })
                     : ([](ZL_Compressor* comp,
                           void* opaque,
                           const ProfileArgs&) {
                           auto graph =
                                   ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
                                           comp,
                                           (ZL_NodeID*)opaque,
                                           1,
                                           ZL_GRAPH_FIELD_LZ);
                           return ZL_Compressor_buildACEGraphWithDefault(
                                   comp, graph);
                       }),
            std::move(nodeid));
}
} // namespace

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
                    auto it = args.argmap.find("TBD");
                    if (it != args.argmap.end()) {
                        auto str = it->second;
                        if (str.size() != 1) {
                            throw InvalidArgsException(
                                    "The CSV profile separator must be a single character. Pass it with --profile-arg <char>.");
                        }
                        return openzl::custom_parsers::
                                ZL_createGraph_genericCSVCompressorWithOptions(
                                        comp,
                                        custom_parsers::kDefaultChunkSize,
                                        true,
                                        str[0],
                                        false);
                    }
                    return openzl::custom_parsers::
                            ZL_createGraph_genericCSVCompressor(comp);
                });

        addLEintProfile(mp, true, 16);
        addLEintProfile(mp, false, 16);
        addLEintProfile(mp, true, 32);
        addLEintProfile(mp, false, 32);
        addLEintProfile(mp, true, 64);
        addLEintProfile(mp, false, 64);

        std::string kParquetName = "parquet";
        mp[kParquetName]         = std::make_shared<CompressProfile>(
                kParquetName,
                "Parquet in the canonical format (no compression, plain encoding)",
                [](ZL_Compressor* comp, void*, const ProfileArgs&) {
                    auto clustering = ZS2_createGraph_genericClustering(comp);
                    return ZL_Parquet_registerGraph(comp, clustering);
                });

        std::string kSDDLName = "sddl";
        mp[kSDDLName]         = std::make_shared<CompressProfile>(
                kSDDLName,
                "Data that can be parsed using the Simple Data Description Language. Pass a path to the data description file with --profile-arg.",
                [](ZL_Compressor* comp, void*, const ProfileArgs& args) {
                    auto it = args.argmap.find("TBD");
                    if (it == args.argmap.end()) {
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

        std::string kSAOName = "sao";
        mp[kSAOName]         = std::make_shared<CompressProfile>(
                kSAOName,
                "SAO format from the Silesia corpus",
                [](ZL_Compressor* comp, void*, const ProfileArgs&) {
                    CompressorRef compressor(comp);
                    return saoProfile(compressor);
                });

        return mp;
    }();
    return staticProfiles;
};

} // namespace openzl::cli
