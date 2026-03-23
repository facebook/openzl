// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <dirent.h>
#include "core_model.h"
#include "openzl/compress/selectors/ml/gbt.h"
#include "openzl/compress/selectors/ml/mlselector.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"

typedef struct {
    size_t totalSize;
    size_t numFiles;
} FileMapResult;

typedef struct {
    size_t numGraphs;
    ZL_LabeledGraphID* graphs;
} LabeledGraphResult;

typedef size_t (
        *FileMapper)(ZL_Compressor* zs2_cgraph, void* data, size_t dataSize);

/**
 * Open each file in the directory and applies the fileMapper function to
 * the file data using the zs2_cgraph.
 */
static FileMapResult mapOverFilesInDirectory(
        char* directoryPath,
        ZL_Compressor* zs2_cgraph,
        FileMapper fileMapper)
{
    // Open the directory
    DIR* directory = opendir(directoryPath);
    struct dirent* directoryEntry;
    FILE* file;

    FileMapResult result = { .numFiles = 0, .totalSize = 0 };

    if (directory == NULL) {
        printf("Error : Failed to open directory - %s\n", directoryPath);
        return (FileMapResult){ .numFiles = 0, .totalSize = 0 };
    }

    // Go through each file in the directory
    while ((directoryEntry = readdir(directory))) {
        if (!strcmp(directoryEntry->d_name, "."))
            continue;
        if (!strcmp(directoryEntry->d_name, ".."))
            continue;

        result.numFiles += 1;

        char* full_path =
                malloc(strlen(directoryPath) + strlen(directoryEntry->d_name)
                       + 1); // +1 for the null terminator

        strcpy(full_path, directoryPath);
        strcat(full_path, directoryEntry->d_name);

        // Open the file and get the file size with/without compression
        file = fopen(full_path, "r");
        if (file == NULL) {
            printf("Error : Failed to open file - %s\n",
                   directoryEntry->d_name);
            return (FileMapResult){ .numFiles = 0, .totalSize = 0 };
        }

        fseek(file, 0, SEEK_END);
        size_t fileSize = (size_t)ftell(file);
        rewind(file);

        char* fileContent = malloc(fileSize + 1);
        size_t bytesRead  = fread(fileContent, 1, fileSize, file);
        if (bytesRead != fileSize) {
            printf("Error: Bytes read does not match file size\n");
            return (FileMapResult){ .numFiles = 0, .totalSize = 0 };
        }
        result.totalSize += fileMapper(zs2_cgraph, fileContent, fileSize + 1);
        free(fileContent);

        fclose(file);
    }
    return result;
}

// Compresses the data using the zs2_cgraph and returns the compressed size.
static size_t
compress(const ZL_Compressor* cgraph, uint64_t* data, size_t dataSize)
{
    size_t compressed        = ZL_compressBound(dataSize * sizeof(*data));
    uint64_t* compressedData = malloc(compressed);

    ZL_CCtx* cctx = ZL_CCtx_create();
    if (ZL_isError(ZL_CCtx_refCompressor(cctx, cgraph))) {
        printf("Failed to set graph\n");
        return dataSize;
    }

    ZL_TypedRef* const tref = ZL_TypedRef_createNumeric(
            data, sizeof(uint64_t), dataSize / sizeof(uint64_t));

    if (ZL_isError(ZL_CCtx_setParameter(
                cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        printf("Failed to set format version\n");
    }
    ZL_Report const csize =
            ZL_CCtx_compressTypedRef(cctx, compressedData, compressed, tref);

    if (ZL_isError(csize)) {
        printf("Compression failed: %s\n",
               ZL_CCtx_getErrorContextString(cctx, csize));
        return dataSize;
    }

    compressed = ZL_validResult(csize);
    ZL_CCtx_free(cctx);
    return compressed;
}

// Return dataSize
static size_t
getDataSize(ZL_Compressor* zs2_cgraph, void* data, size_t dataSize)
{
    (void)zs2_cgraph;
    (void)data;
    return dataSize;
}

// Compresses the data using the zs2_cgraph and returns the compressed size.
static size_t
compressData(ZL_Compressor* zs2_cgraph, void* data, size_t dataSize)
{
    return compress(zs2_cgraph, data, dataSize);
}

/**
 * Generates the successors as an array of labeled graphs for the selector.
 * Passes ownership of the graphs inside LabeledGraphResult to the caller of
 * this function - the caller must free the graphs.
 */
static LabeledGraphResult generateSuccessors(ZL_Compressor* cgraph)
{
    ZL_GraphID fieldlz = ZL_Compressor_registerFieldLZGraph(cgraph);

    ZL_GraphID range_pack = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_RANGE_PACK, &fieldlz, 1);
    ZL_GraphID range_pack_zstd = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_RANGE_PACK, &ZL_GRAPH_ZSTD, 1);

    ZL_GraphID delta_fieldlz = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_DELTA_INT, &fieldlz, 1);
    ZL_GraphID tokenize_delta_fieldlz = ZL_Compressor_registerTokenizeGraph(
            cgraph, ZL_Type_numeric, /* sort */ true, delta_fieldlz, fieldlz);

    ZL_LabeledGraphID temp_graphs[6] = {
        { .label = "delta_fieldlz", .graph = delta_fieldlz },
        { .label = "fieldlz", .graph = fieldlz },
        { .label = "range_pack", .graph = range_pack },
        { .label = "range_pack_zstd", .graph = range_pack_zstd },
        { .label = "tokenize_delta_fieldlz", .graph = tokenize_delta_fieldlz },
        { .label = "zstd", .graph = ZL_GRAPH_ZSTD }
    };

    ZL_LabeledGraphID* graphs = malloc(sizeof(ZL_LabeledGraphID) * 6);
    if (graphs != NULL) {
        memcpy(graphs, temp_graphs, sizeof(ZL_LabeledGraphID) * 6);
    }

    return (LabeledGraphResult){ .graphs = graphs, .numGraphs = 6 };
}

/**
 * Creates a typed selector based on GBTModel
 */
static ZL_Compressor* generateInferenceGraph(const GBTModel* model)
{
    ZL_Compressor* cgraph         = ZL_Compressor_create();
    LabeledGraphResult successors = generateSuccessors(cgraph);
    if (successors.graphs == NULL) {
        printf("Failed to generate successors\n");
        return NULL;
    }

    if (ZL_isError(ZL_Compressor_selectStartingGraphID(
                cgraph,
                ZL_Compressor_registerGBTModelGraph(
                        cgraph,
                        model,
                        successors.graphs,
                        successors.numGraphs)))) {
        printf("Failed to register inference selector\n");
        return NULL;
    }
    free(successors.graphs);
    return cgraph;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("No directory path provided.\n");
        return 0;
    }

    char* directoryPath = argv[1];

    bool useGenericGraph = false;
    if (argc >= 3) {
        if (strcmp(argv[2], "-g") == 0) {
            useGenericGraph = true;
            printf("Using generic numeric graph\n");
        } else if (strcmp(argv[2], "--help") == 0) {
            printf("Usage: <directory> [-g] \n\t -g: Flag to indicate usage of generic numeric graph\n");
            return 0;
        } else {
            printf("Using generated example core model\n");
        }
    }

    ZL_Compressor* zs2_cgraph = NULL;
    if (useGenericGraph) {
        zs2_cgraph = ZL_Compressor_create();
        ZL_REQUIRE(!ZL_isError(ZL_Compressor_selectStartingGraphID(
                zs2_cgraph, ZL_GRAPH_NUMERIC)));
    } else {
        const GBTModel gbtModel =
                getExampleCoreModelGbtModel(FeatureGen_integer);
        zs2_cgraph = generateInferenceGraph(&gbtModel);
    }

    if (zs2_cgraph == NULL) {
        printf("Failed to create cgraph\n");
        return 0;
    }

    FileMapResult original =
            mapOverFilesInDirectory(directoryPath, zs2_cgraph, getDataSize);
    FileMapResult compressed =
            mapOverFilesInDirectory(directoryPath, zs2_cgraph, compressData);

    ZL_ASSERT_EQ(original.numFiles, compressed.numFiles);

    printf("Completed compression of %zu files with x%.2f CR (%zu -> %zu)\n",
           original.numFiles,
           (float)original.totalSize / (float)compressed.totalSize,
           original.totalSize,
           compressed.totalSize);

    ZL_Compressor_free(zs2_cgraph);
}
