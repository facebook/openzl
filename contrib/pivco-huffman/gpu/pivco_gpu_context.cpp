// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/pivco-huffman/gpu/pivco_gpu.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "contrib/pivco-huffman/gpu/pivco_gpu_tree.h"
#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"

namespace {

constexpr uint16_t kNoScheduleChild = UINT16_MAX;

size_t scheduleStageIndex(uint16_t level, uint8_t op)
{
    return static_cast<size_t>(level) * PIVCO_GPU_SCHEDULE_OP_COUNT + op;
}

bool scheduleNodeMaterializes(const PivCoGpuScheduleNode& node)
{
    return node.kind != PIVCO_GPU_SCHEDULE_CONSTANT;
}

PivCoGpuScheduleOp flatOpForDepth(size_t depth)
{
    return static_cast<PivCoGpuScheduleOp>(
            PIVCO_GPU_SCHEDULE_OP_FLAT1 + depth - 1);
}

uint16_t buildScheduleNode(
        PivCoGpuDecodeSchedule* schedule,
        const ZL_PivCoHuffmanTree* tree,
        size_t level,
        size_t firstRank,
        size_t rankEnd,
        bool* ok)
{
    if (!*ok || schedule->nodeCount >= PIVCO_GPU_MAX_TREE_NODES
        || level >= PIVCO_GPU_MAX_LEVELS || firstRank >= rankEnd
        || rankEnd > tree->numRanks) {
        *ok = false;
        return 0;
    }

    const uint16_t nodeIndex   = schedule->nodeCount++;
    PivCoGpuScheduleNode& node = schedule->nodes[nodeIndex];
    node.leftChild             = kNoScheduleChild;
    node.rightChild            = kNoScheduleChild;
    node.firstRank             = static_cast<uint16_t>(firstRank);
    node.rankEnd               = static_cast<uint16_t>(rankEnd);
    node.kind                  = PIVCO_GPU_SCHEDULE_INTERNAL;
    node.op                    = 0;
    node.level                 = static_cast<uint8_t>(level);
    node.flatDepth             = 0;
    node.symbol                = 0;
    node.leftSymbol            = 0;
    node.rightSymbol           = 0;
    node.reserved              = 0;
    if (schedule->maxLevel < level) {
        schedule->maxLevel = static_cast<uint16_t>(level);
    }

    if (ZL_PivCoHuffmanTree_rangeIsLeaf(tree, firstRank, rankEnd)) {
        const size_t depth = ZL_PivCoHuffmanTree_leafFlatDepth(tree, firstRank);
        if (depth == 0) {
            node.kind   = PIVCO_GPU_SCHEDULE_CONSTANT;
            node.symbol = tree->rankToSymbol[firstRank];
            return nodeIndex;
        }
        if (depth > 8) {
            *ok = false;
            return nodeIndex;
        }
        node.kind      = PIVCO_GPU_SCHEDULE_FLAT;
        node.flatDepth = static_cast<uint8_t>(depth);
        node.op        = static_cast<uint8_t>(flatOpForDepth(depth));
        return nodeIndex;
    }

    if (level >= tree->numLevels) {
        *ok = false;
        return nodeIndex;
    }

    const size_t splitRank =
            ZL_PivCoHuffmanTree_splitRank(tree, level, firstRank, rankEnd);
    if (splitRank <= firstRank || splitRank >= rankEnd) {
        *ok = false;
        return nodeIndex;
    }

    const uint16_t leftChild = buildScheduleNode(
            schedule, tree, level + 1, firstRank, splitRank, ok);
    const uint16_t rightChild = buildScheduleNode(
            schedule, tree, level + 1, splitRank, rankEnd, ok);
    if (!*ok) {
        return nodeIndex;
    }

    node.leftChild  = leftChild;
    node.rightChild = rightChild;

    const PivCoGpuScheduleNode& left  = schedule->nodes[leftChild];
    const PivCoGpuScheduleNode& right = schedule->nodes[rightChild];
    const bool leftIsConstant  = left.kind == PIVCO_GPU_SCHEDULE_CONSTANT;
    const bool rightIsConstant = right.kind == PIVCO_GPU_SCHEDULE_CONSTANT;
    if (leftIsConstant) {
        node.leftSymbol = left.symbol;
    }
    if (rightIsConstant) {
        node.rightSymbol = right.symbol;
    }

    if (leftIsConstant && rightIsConstant) {
        node.op = PIVCO_GPU_SCHEDULE_OP_MERGE_CONSTANT_CONSTANT;
    } else if (leftIsConstant) {
        node.op = PIVCO_GPU_SCHEDULE_OP_MERGE_CONSTANT_VECTOR;
    } else if (rightIsConstant) {
        node.op = PIVCO_GPU_SCHEDULE_OP_MERGE_VECTOR_CONSTANT;
    } else {
        node.op = PIVCO_GPU_SCHEDULE_OP_MERGE_VECTOR_VECTOR;
    }
    return nodeIndex;
}

void buildScheduleStages(PivCoGpuDecodeSchedule* schedule)
{
    uint16_t counts[PIVCO_GPU_MAX_LEVELS * PIVCO_GPU_SCHEDULE_OP_COUNT] = {};
    for (uint16_t i = 0; i < schedule->nodeCount; ++i) {
        const PivCoGpuScheduleNode& node = schedule->nodes[i];
        if (node.kind == PIVCO_GPU_SCHEDULE_INTERNAL) {
            schedule->internalItems[schedule->internalCount++] =
                    PivCoGpuScheduleStageItem{ i };
        }
        if (scheduleNodeMaterializes(node)) {
            ++counts[scheduleStageIndex(node.level, node.op)];
        }
    }

    uint16_t cursor = 0;
    for (size_t i = 0; i < PIVCO_GPU_MAX_LEVELS * PIVCO_GPU_SCHEDULE_OP_COUNT;
         ++i) {
        schedule->stageOffset[i] = cursor;
        schedule->stageCount[i]  = counts[i];
        cursor += counts[i];
        counts[i] = schedule->stageOffset[i];
    }

    for (uint16_t i = 0; i < schedule->nodeCount; ++i) {
        const PivCoGpuScheduleNode& node = schedule->nodes[i];
        if (!scheduleNodeMaterializes(node)) {
            continue;
        }
        const size_t index = scheduleStageIndex(node.level, node.op);
        schedule->stageItems[counts[index]++] = PivCoGpuScheduleStageItem{ i };
    }
}

void fillDecodeSchedule(
        PivCoGpuDecodeSchedule* schedule,
        const ZL_PivCoHuffmanTree* tree)
{
    std::memset(schedule, 0, sizeof(*schedule));
    if (tree->numRanks == 0) {
        return;
    }

    bool ok = true;
    const uint16_t root =
            buildScheduleNode(schedule, tree, 0, 0, tree->numRanks, &ok);
    if (!ok || root != 0 || schedule->nodeCount > PIVCO_GPU_MAX_TREE_NODES) {
        std::memset(schedule, 0, sizeof(*schedule));
        return;
    }
    buildScheduleStages(schedule);
    schedule->enabled = 1;
}

void fillGpuTree(
        PivCoGpuTree* dst,
        const ZL_PivCoHuffmanTree* src,
        const uint8_t* weights,
        size_t weightsSize)
{
    std::memset(dst, 0, sizeof(*dst));
    std::memcpy(
            dst->symbolToRank, src->symbolToRank, sizeof(src->symbolToRank));
    std::memcpy(
            dst->rankToSymbol, src->rankToSymbol, sizeof(src->rankToSymbol));
    std::memcpy(
            dst->rankToFlatDepth,
            src->rankToFlatDepth,
            sizeof(src->rankToFlatDepth));
    std::memcpy(
            dst->rankToCodeword,
            src->rankToCodeword,
            sizeof(src->rankToCodeword));
    for (size_t i = 0; i < weightsSize; ++i) {
        dst->symbolPresent[i] = weights[i] != 0;
    }
    dst->numLevels = src->numLevels;
    dst->numRanks  = src->numRanks;
    dst->tableLog  = src->tableLog;

    if (src->numRanks > 1
        && ZL_PivCoHuffmanTree_rangeIsLeaf(src, 0, src->numRanks)) {
        dst->fastMode = PIVCO_GPU_FAST_FLAT_ROOT;
        return;
    }

    if (src->numRanks == 3
        && !ZL_PivCoHuffmanTree_rangeIsLeaf(src, 0, src->numRanks)) {
        const uint16_t splitRank =
                ZL_PivCoHuffmanTree_splitRank(src, 0, 0, src->numRanks);
        const bool rhsIsFlatOneBit =
                ZL_PivCoHuffmanTree_rangeIsLeaf(src, splitRank, src->numRanks)
                && ZL_PivCoHuffmanTree_leafFlatDepth(src, splitRank) == 1;
        bool rhsIsTwoConstants = false;
        if (!rhsIsFlatOneBit
            && !ZL_PivCoHuffmanTree_rangeIsLeaf(
                    src, splitRank, src->numRanks)) {
            const uint16_t rhsSplitRank = ZL_PivCoHuffmanTree_splitRank(
                    src, 1, splitRank, src->numRanks);
            rhsIsTwoConstants = rhsSplitRank == 2
                    && ZL_PivCoHuffmanTree_rangeIsConstantLeaf(
                                        src, splitRank, rhsSplitRank)
                    && ZL_PivCoHuffmanTree_rangeIsConstantLeaf(
                                        src, rhsSplitRank, src->numRanks);
        }

        if (splitRank == 1
            && ZL_PivCoHuffmanTree_rangeIsConstantLeaf(src, 0, splitRank)
            && (rhsIsFlatOneBit || rhsIsTwoConstants)) {
            dst->fastMode           = PIVCO_GPU_FAST_ROOT_CONST_FLAT1;
            dst->fastZeroSymbol     = src->rankToSymbol[0];
            dst->fastLeafZeroSymbol = src->rankToSymbol[1];
            dst->fastLeafOneSymbol  = src->rankToSymbol[2];
        }
    }
}

} // namespace

extern "C" ZL_Report pivcoGpuContextCreate(
        PivCoGpuContext** context,
        const uint8_t* weights,
        size_t weightsSize,
        int tableLog)
{
    if (context == nullptr || weights == nullptr || weightsSize == 0
        || weightsSize > PIVCO_GPU_MAX_SYMBOLS) {
        return ZL_returnError(ZL_ErrorCode_parameter_invalid);
    }

    const int computedTableLog =
            ZL_PivCoHuffman_computeTableLog(weights, weightsSize);
    if (computedTableLog < 0
        || (tableLog >= 0 && tableLog != computedTableLog)) {
        return ZL_returnError(ZL_ErrorCode_corruption);
    }

    ZL_PivCoHuffmanTree cpuTree;
    ZL_PivCoHuffmanTree_build(&cpuTree, weights, weightsSize, computedTableLog);

    PivCoGpuContext* created = new (std::nothrow) PivCoGpuContext{};
    if (created == nullptr) {
        return ZL_returnError(ZL_ErrorCode_allocation);
    }
    fillGpuTree(&created->hostTree, &cpuTree, weights, weightsSize);
    fillDecodeSchedule(&created->decodeSchedule, &cpuTree);

    *context = created;
    return ZL_returnSuccess();
}

extern "C" void pivcoGpuContextDestroy(PivCoGpuContext* context)
{
    if (context == nullptr) {
        return;
    }
    delete context;
}
