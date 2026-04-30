// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <dlfcn.h>

#include <memory>
#include <vector>

namespace openzl::detail {
enum class VersionType {
    MAJOR = 0,
    MINOR = 1,
    PATCH = 2,
    // DEFAULT_FORMAT = 3,
    MIN_FORMAT = 4,
    MAX_FORMAT = 5,
};
} // namespace openzl::detail

extern "C" unsigned VersionTestInterface_getZStrongVersion(int versionType);

extern "C" size_t VersionTestInterface_getNbNodeIDs();
extern "C" void VersionTestInterface_getAllNodeIDs(
        int* nodeIDs,
        int* transformIDs,
        size_t nodesCapacity);

extern "C" size_t VersionTestInterface_getNbGraphIDs();
extern "C" void VersionTestInterface_getAllGraphIDs(
        int* graphs,
        size_t nodesCapacity);
extern "C" int VersionTestInterface_getTransformID(int nodeID);

extern "C" size_t VersionTestInterface_compressBound(size_t srcSize);

extern "C" bool VersionTestInterface_isError(size_t ret);

extern "C" size_t VersionTestInterface_compressWithNodeID(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        unsigned eltWidth,
        unsigned formatVersion,
        int node);
extern "C" size_t VersionTestInterface_compressWithGraphID(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        unsigned eltWidth,
        unsigned formatVersion,
        int graph);
extern "C" size_t VersionTestInterface_compressWithGraphFromEntropy(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        unsigned eltWidth,
        unsigned formatVersion,
        void const* entropy,
        size_t entropySize);

extern "C" size_t VersionTestInterface_decompressedSize(
        void const* src,
        size_t srcSize);

extern "C" size_t VersionTestInterface_decompress(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize);

extern "C" size_t VersionTestInterface_customNodeData(
        char** bufferPtr,
        size_t** eltWidthsPtr,
        size_t** sizesPtr,
        int node);

extern "C" size_t VersionTestInterface_customGraphData(
        char** bufferPtr,
        size_t** eltWidthsPtr,
        size_t** sizesPtr,
        int graph);
