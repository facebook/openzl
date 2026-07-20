// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t PIVCO_GPU_MAX_BLOCK_SIZE = size_t{ 1 } << 28;
constexpr size_t PIVCO_GPU_MAX_SYMBOLS    = 256;
constexpr size_t PIVCO_GPU_MAX_TREE_NODES = 2 * PIVCO_GPU_MAX_SYMBOLS - 1;
constexpr size_t PIVCO_GPU_MAX_LEVELS     = 16;

enum PivCoGpuFastMode : uint8_t {
    PIVCO_GPU_FAST_NONE             = 0,
    PIVCO_GPU_FAST_ROOT_CONST_FLAT1 = 1,
    PIVCO_GPU_FAST_FLAT_ROOT        = 2,
};

struct PivCoGpuTree {
    uint8_t symbolToRank[PIVCO_GPU_MAX_SYMBOLS];
    uint8_t symbolPresent[PIVCO_GPU_MAX_SYMBOLS];
    uint8_t rankToSymbol[PIVCO_GPU_MAX_SYMBOLS];
    uint8_t rankToFlatDepth[PIVCO_GPU_MAX_SYMBOLS];
    uint16_t rankToCodeword[PIVCO_GPU_MAX_SYMBOLS];
    uint16_t numLevels;
    uint16_t numRanks;
    int tableLog;
    uint8_t fastMode;
    uint8_t fastZeroSymbol;
    uint8_t fastLeafZeroSymbol;
    uint8_t fastLeafOneSymbol;
};

enum PivCoGpuScheduleNodeKind : uint8_t {
    PIVCO_GPU_SCHEDULE_CONSTANT = 0,
    PIVCO_GPU_SCHEDULE_FLAT     = 1,
    PIVCO_GPU_SCHEDULE_INTERNAL = 2,
};

enum PivCoGpuScheduleOp : uint8_t {
    PIVCO_GPU_SCHEDULE_OP_FLAT1                   = 0,
    PIVCO_GPU_SCHEDULE_OP_FLAT2                   = 1,
    PIVCO_GPU_SCHEDULE_OP_FLAT3                   = 2,
    PIVCO_GPU_SCHEDULE_OP_FLAT4                   = 3,
    PIVCO_GPU_SCHEDULE_OP_FLAT5                   = 4,
    PIVCO_GPU_SCHEDULE_OP_FLAT6                   = 5,
    PIVCO_GPU_SCHEDULE_OP_FLAT7                   = 6,
    PIVCO_GPU_SCHEDULE_OP_FLAT8                   = 7,
    PIVCO_GPU_SCHEDULE_OP_MERGE_VECTOR_VECTOR     = 8,
    PIVCO_GPU_SCHEDULE_OP_MERGE_CONSTANT_VECTOR   = 9,
    PIVCO_GPU_SCHEDULE_OP_MERGE_VECTOR_CONSTANT   = 10,
    PIVCO_GPU_SCHEDULE_OP_MERGE_CONSTANT_CONSTANT = 11,
    PIVCO_GPU_SCHEDULE_OP_COUNT                   = 12,
};

struct PivCoGpuScheduleNode {
    uint16_t leftChild;
    uint16_t rightChild;
    uint16_t firstRank;
    uint16_t rankEnd;
    uint8_t kind;
    uint8_t op;
    uint8_t level;
    uint8_t flatDepth;
    uint8_t symbol;
    uint8_t leftSymbol;
    uint8_t rightSymbol;
    uint8_t reserved;
};

struct PivCoGpuScheduleStageItem {
    uint16_t node;
};

struct PivCoGpuDecodeSchedule {
    PivCoGpuScheduleNode nodes[PIVCO_GPU_MAX_TREE_NODES];
    PivCoGpuScheduleStageItem stageItems[PIVCO_GPU_MAX_TREE_NODES];
    PivCoGpuScheduleStageItem internalItems[PIVCO_GPU_MAX_TREE_NODES];
    uint16_t stageOffset[PIVCO_GPU_MAX_LEVELS * PIVCO_GPU_SCHEDULE_OP_COUNT];
    uint16_t stageCount[PIVCO_GPU_MAX_LEVELS * PIVCO_GPU_SCHEDULE_OP_COUNT];
    uint16_t nodeCount;
    uint16_t internalCount;
    uint16_t maxLevel;
    uint8_t enabled;
};

struct PivCoGpuContext {
    PivCoGpuTree hostTree;
    PivCoGpuDecodeSchedule decodeSchedule;
};
