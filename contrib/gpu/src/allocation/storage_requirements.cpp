// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/allocation/storage_requirements.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"

namespace openzl::gpu {
namespace {

/**
 * Finds the natural alignment for the primary data region of @p shape.
 *
 * The alignment is at most 16 bytes. A non-power-of-two width has byte
 * alignment because it has no natural power-of-two boundary.
 */
size_t naturalDataAlignment(const StreamShape& shape)
{
    const size_t width = shape.eltWidth;
    if (width == 0 || (width & (width - 1)) != 0) {
        return 1;
    }
    return std::min<size_t>(width, 16);
}

/**
 * Finds the alignment that core requires of a caller's buffer for a final
 * output with @p shape.
 *
 * Core checks alignment only for numeric outputs. @c planDecode has already
 * checked their width.
 */
size_t destinationDataAlignment(const StreamShape& shape)
{
    return shape.type == ZL_Type_numeric
            ? MEM_alignmentForNumericWidth(shape.eltWidth)
            : 1;
}

/**
 * Finds how long storage is needed for every stream created during decoding.
 *
 * Storage is needed starting when the codec run that creates the stream runs.
 * It remains needed through the codec run that reads the stream. If no codec
 * run reads it, it is needed only by the run that creates it. Streams read
 * directly from the frame do not need workspace storage.
 */
std::vector<StorageLifetime> deriveProducedStreamLifetimes(
        const DecodePlan& plan)
{
    std::vector<StorageLifetime> streamLifetimes(plan.streams.size());
    for (size_t streamIndex = 0; streamIndex < plan.streams.size();
         ++streamIndex) {
        const PlannedStream& plannedStream = plan.streams.at(streamIndex);
        if (!plannedStream.producerCodecRun.has_value()) {
            continue;
        }
        const PlannedCodecRunIndex producerCodecRunIndex =
                *plannedStream.producerCodecRun;
        const std::optional<PlannedCodecRunIndex>& consumerCodecRunIndex =
                plannedStream.consumerCodecRun;

        const DecodeExecutionStage firstUse =
                DecodeExecutionStage::codecRun(producerCodecRunIndex);
        const DecodeExecutionStage lastUse = consumerCodecRunIndex.has_value()
                ? DecodeExecutionStage::codecRun(*consumerCodecRunIndex)
                : firstUse;
        streamLifetimes.at(streamIndex)    = {
               .firstUse = firstUse,
               .lastUse  = lastUse,
        };
    }
    return streamLifetimes;
}

/**
 * Adds a non-empty region of @p stream to @p requirements, and records its
 * index for @p stream.
 *
 * A zero-byte region does not need address placement, so this function omits
 * it.
 */
void addStorageRequirement(
        ExecutionRequirements& requirements,
        PlannedStreamIndex stream,
        const StorageRequirement& requirement)
{
    if (requirement.size == 0) {
        return;
    }
    requirements.streamRequirements.at(stream).of(requirement.region) =
            requirements.requirements.size();
    requirements.requirements.push_back(requirement);
}

/**
 * Reports whether @p earlierCodecRunIndex must finish before
 * @p laterCodecRunIndex can run.
 *
 * The search starts at @p laterCodecRunIndex and follows its dependencies. The
 * function returns true if the search reaches @p earlierCodecRunIndex.
 *
 * Every dependency precedes its dependent, because @c planDecode records codec
 * runs in frame order. So no codec run at or below @p earlierCodecRunIndex can
 * lead back to it, and the search visits only the codec runs between the two.
 */
bool codecRunHappensBefore(
        const DecodePlan& plan,
        PlannedCodecRunIndex earlierCodecRunIndex,
        PlannedCodecRunIndex laterCodecRunIndex,
        std::vector<bool>& visitedCodecRuns,
        std::vector<PlannedCodecRunIndex>& pendingCodecRuns)
{
    if (earlierCodecRunIndex >= laterCodecRunIndex) {
        return false;
    }
    // Element i records whether codec run earlierCodecRunIndex + 1 + i was
    // visited.
    visitedCodecRuns.assign(laterCodecRunIndex - earlierCodecRunIndex, false);
    pendingCodecRuns.clear();
    pendingCodecRuns.push_back(laterCodecRunIndex);
    while (!pendingCodecRuns.empty()) {
        const PlannedCodecRunIndex codecRunIndex = pendingCodecRuns.back();
        pendingCodecRuns.pop_back();
        const size_t visitedIndex = codecRunIndex - earlierCodecRunIndex - 1;
        if (visitedCodecRuns.at(visitedIndex)) {
            continue;
        }
        visitedCodecRuns.at(visitedIndex) = true;

        for (const PlannedCodecRunIndex dependencyCodecRunIndex :
             plan.dependenciesOf(plan.codecRuns.at(codecRunIndex))) {
            if (dependencyCodecRunIndex == earlierCodecRunIndex) {
                return true;
            }
            if (dependencyCodecRunIndex > earlierCodecRunIndex) {
                pendingCodecRuns.push_back(dependencyCodecRunIndex);
            }
        }
    }
    return false;
}

/**
 * Chooses a safe final use for storage shared by two streams.
 *
 * If one use must happen after the other, returns the later one. If the uses
 * are independent, returns the end of the decode so the storage cannot be
 * reused while either stream may still need it.
 */
DecodeExecutionStage mergeStorageLastUses(
        const DecodePlan& plan,
        DecodeExecutionStage currentLastUse,
        DecodeExecutionStage candidateLastUse,
        std::vector<bool>& visitedCodecRuns,
        std::vector<PlannedCodecRunIndex>& pendingCodecRuns)
{
    if (currentLastUse == candidateLastUse
        // Decode start is always the earliest stage.
        || candidateLastUse.kind == DecodeExecutionStageKind::DecodeStart
        // No stage can occur after decode end.
        || currentLastUse.kind == DecodeExecutionStageKind::DecodeEnd) {
        return currentLastUse;
    }
    if (
            // Any remaining candidate occurs after decode start.
            currentLastUse.kind == DecodeExecutionStageKind::DecodeStart
            // Decode end occurs after every other stage.
            || candidateLastUse.kind == DecodeExecutionStageKind::DecodeEnd) {
        return candidateLastUse;
    }
    if (codecRunHappensBefore(
                plan,
                *currentLastUse.codecRunIndex,
                *candidateLastUse.codecRunIndex,
                visitedCodecRuns,
                pendingCodecRuns)) {
        return candidateLastUse;
    }
    if (codecRunHappensBefore(
                plan,
                *candidateLastUse.codecRunIndex,
                *currentLastUse.codecRunIndex,
                visitedCodecRuns,
                pendingCodecRuns)) {
        return currentLastUse;
    }
    return DecodeExecutionStage::decodeEnd();
}

/**
 * Keeps the data that each alias reads valid and aligned for that alias.
 *
 * Follows each alias to the stream that owns the data. If that data is in
 * workspace storage, extends its lifetime through the alias's final use and
 * raises @p aliasAlignments for it to the alias's natural alignment. An alias
 * whose offset is a multiple of its alignment then starts at an aligned
 * address.
 *
 * Records the owner of every alias it resolves, so each alias chain is walked
 * once in total.
 */
void extendOwnersForAliases(
        const DecodePlan& plan,
        std::vector<StorageLifetime>& streamLifetimes,
        std::vector<size_t>& aliasAlignments)
{
    std::vector<std::optional<PlannedStreamIndex>> ownerByStream(
            plan.streams.size());
    std::vector<PlannedStreamIndex> unresolvedAliases;
    std::vector<bool> visitedCodecRuns{};
    std::vector<PlannedCodecRunIndex> pendingCodecRuns{};
    for (size_t aliasStreamIndex = 0; aliasStreamIndex < plan.streams.size();
         ++aliasStreamIndex) {
        const PlannedStream& aliasStream = plan.streams.at(aliasStreamIndex);
        if (aliasStream.storageClass != StorageClass::StreamRef) {
            continue;
        }

        // Walk to a stream that is not an alias, or to an alias whose owner is
        // already known. @c planDecode points every alias at an input of its
        // producer, so the walk ends.
        PlannedStreamIndex streamIndex = aliasStreamIndex;
        unresolvedAliases.clear();
        while (plan.streams.at(streamIndex).storageClass
                       == StorageClass::StreamRef
               && !ownerByStream.at(streamIndex).has_value()) {
            unresolvedAliases.push_back(streamIndex);
            streamIndex = *plan.streams.at(streamIndex).aliasStream;
        }
        const PlannedStreamIndex ownerStreamIndex =
                plan.streams.at(streamIndex).storageClass
                        == StorageClass::StreamRef
                ? *ownerByStream.at(streamIndex)
                : streamIndex;
        for (const PlannedStreamIndex resolved : unresolvedAliases) {
            ownerByStream.at(resolved) = ownerStreamIndex;
        }

        // Stored and destination data needs no workspace.
        if (plan.streams.at(ownerStreamIndex).storageClass
            == StorageClass::StreamArena) {
            streamLifetimes.at(ownerStreamIndex).lastUse = mergeStorageLastUses(
                    plan,
                    streamLifetimes.at(ownerStreamIndex).lastUse,
                    streamLifetimes.at(aliasStreamIndex).lastUse,
                    visitedCodecRuns,
                    pendingCodecRuns);
            aliasAlignments.at(ownerStreamIndex) = std::max(
                    aliasAlignments.at(ownerStreamIndex),
                    naturalDataAlignment(aliasStream.shape));
        }
    }
}

ExecutionRequirements buildRequirements(const DecodePlan& plan)
{
    // Step 1: The function derives the producer-to-consumer lifetime of each
    // produced stream.
    std::vector<StorageLifetime> streamLifetimes =
            deriveProducedStreamLifetimes(plan);

    ExecutionRequirements requirements;
    requirements.streamRequirements.resize(plan.streams.size());

    const StorageLifetime wholeDecodeLifetime{
        .firstUse = DecodeExecutionStage::decodeStart(),
        .lastUse  = DecodeExecutionStage::decodeEnd(),
    };

    // Step 2: The function reserves destination regions for the final outputs
    // during the whole decode. Core gives each final output its own buffer and
    // appends each chunk's contribution in chunk order, so the regions are
    // ordered by final output and then by chunk. planDecode checks that every
    // chunk has the same final outputs.
    const size_t numFinalOutputs =
            plan.chunks.empty() ? 0 : plan.chunks.front().numOutputs;
    std::vector<bool> isFinalOutput(plan.streams.size());
    for (size_t outputIndex = 0; outputIndex < numFinalOutputs; ++outputIndex) {
        for (size_t chunkIndex = 0; chunkIndex < plan.chunks.size();
             ++chunkIndex) {
            const PlannedStreamIndex outputStreamIndex =
                    plan.finalOutputOf(chunkIndex, outputIndex);
            const PlannedStream& outputStream =
                    plan.streams.at(outputStreamIndex);
            if (outputStream.storageClass == StorageClass::StreamRef) {
                // The executor reads this alias during the final output copy.
                streamLifetimes.at(outputStreamIndex).lastUse =
                        DecodeExecutionStage::decodeEnd();
            }
            // A string stream keeps its `uint32_t` lengths in a table separate
            // from its data. Other streams have no table, so the second call
            // adds nothing for them.
            addStorageRequirement(
                    requirements,
                    outputStreamIndex,
                    {
                            .region      = StreamRegion::Data,
                            .arena       = ArenaClass::Destination,
                            .finalOutput = outputIndex,
                            .lifetime    = wholeDecodeLifetime,
                            .size        = outputStream.storageSize.dataBytes,
                            .alignment   = destinationDataAlignment(
                                    outputStream.shape),
                    });
            addStorageRequirement(
                    requirements,
                    outputStreamIndex,
                    {
                            .region      = StreamRegion::StringLengths,
                            .arena       = ArenaClass::Destination,
                            .finalOutput = outputIndex,
                            .lifetime    = wholeDecodeLifetime,
                            .size = outputStream.storageSize.stringLengthsBytes,
                            .alignment = alignof(uint32_t),
                    });
            isFinalOutput.at(outputStreamIndex) = true;
        }
    }

    // Step 3: The function extends and aligns the workspace data that aliases
    // read. An alias reads only its owner's data, so an owner's string-length
    // table keeps the lifetime it has before this step.
    const std::vector<StorageLifetime> lengthLifetimes = streamLifetimes;
    std::vector<size_t> aliasAlignments(plan.streams.size(), 1);
    extendOwnersForAliases(plan, streamLifetimes, aliasAlignments);

    // Step 4: The function adds workspace regions for materialized
    // intermediate streams.
    for (size_t streamIndex = 0; streamIndex < plan.streams.size();
         ++streamIndex) {
        const PlannedStream& plannedStream = plan.streams.at(streamIndex);
        if (plannedStream.storageClass == StorageClass::Stored
            || plannedStream.storageClass == StorageClass::Destination) {
            continue;
        }

        if (plannedStream.storageClass == StorageClass::StreamArena) {
            addStorageRequirement(
                    requirements,
                    streamIndex,
                    {
                            .region    = StreamRegion::Data,
                            .arena     = ArenaClass::StreamArena,
                            .lifetime  = streamLifetimes.at(streamIndex),
                            .size      = plannedStream.storageSize.dataBytes,
                            .alignment = std::max(
                                    naturalDataAlignment(plannedStream.shape),
                                    aliasAlignments.at(streamIndex)),
                    });
        }
        // An alias shares only the data of its backing stream, so it needs its
        // own string-length table. A final alias's codec run writes that table
        // into the destination region from Step 2, so it needs none here.
        if (!isFinalOutput.at(streamIndex)) {
            addStorageRequirement(
                    requirements,
                    streamIndex,
                    {
                            .region   = StreamRegion::StringLengths,
                            .arena    = ArenaClass::StreamArena,
                            .lifetime = lengthLifetimes.at(streamIndex),
                            .size     = plannedStream.storageSize
                                            .stringLengthsBytes,
                            .alignment = alignof(uint32_t),
                    });
        }
    }
    return requirements;
}

} // namespace

std::optional<size_t>& StreamRequirements::of(StreamRegion region)
{
    // No default case, so a new region is a -Wswitch warning here.
    switch (region) {
        case StreamRegion::Data:
            return data;
        case StreamRegion::StringLengths:
            return stringLengths;
    }
    ZL_ASSERT_FAIL("Unknown stream region");
    return data;
}

Result<ExecutionRequirements> buildDecodeStorageRequirements(
        const DecodePlan& plan)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    try {
        return buildRequirements(plan);
    } catch (const std::bad_alloc&) {
        ZL_ERR(allocation);
    }
}

} // namespace openzl::gpu
