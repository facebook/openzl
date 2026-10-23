// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "contrib/gpu/src/allocation/storage_requirements.hpp"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_errors.h"

namespace openzl::gpu {
namespace {

constexpr ZL_IDType kCodec = 1;

PublicTransformInfo standardTransform()
{
    PublicTransformInfo transform{};
    transform.trt  = trt_standard;
    transform.trid = kCodec;
    return transform;
}

PlannedStreamIndex addStream(
        DecodePlan& plan,
        StorageClass storageClass,
        std::optional<PlannedCodecRunIndex> producerCodecRun,
        size_t dataBytes,
        size_t elementWidth                           = 1,
        size_t stringLengthsBytes                     = 0,
        std::optional<PlannedStreamIndex> aliasStream = std::nullopt,
        ZL_Type type                                  = ZL_Type_serial)
{
    // A string stream counts strings and has no element width.
    const bool isString = type == ZL_Type_string;
    plan.streams.push_back(
            {
                    .shape = {
                            .type     = type,
                            .numElts  = isString
                                     ? stringLengthsBytes / sizeof(uint32_t)
                                     : elementWidth == 0 ? 0
                                                         : dataBytes / elementWidth,
                            .eltWidth = isString ? 0 : elementWidth,
                    },
                    .storageSize = {
                            .dataBytes          = dataBytes,
                            .stringLengthsBytes = stringLengthsBytes,
                    },
                    .storageClass     = storageClass,
                    .producerCodecRun = producerCodecRun,
                    .aliasStream      = aliasStream,
                    .aliasOffset      = 0,
            });
    return plan.streams.size() - 1;
}

PlannedCodecRunIndex addCodecRun(
        DecodePlan& plan,
        const std::vector<PlannedCodecRunIndex>& dependencies)
{
    const size_t begin = plan.codecRunIndices.size();
    plan.codecRunIndices.insert(
            plan.codecRunIndices.end(),
            dependencies.begin(),
            dependencies.end());
    plan.codecRuns.push_back(
            {
                    .transform = standardTransform(),
                    .dependencies = {
                            .begin = begin,
                            .count = dependencies.size(),
                    },
            });
    return plan.codecRuns.size() - 1;
}

void setOutputs(
        DecodePlan& plan,
        PlannedCodecRunIndex codecRun,
        const std::vector<PlannedStreamIndex>& outputs)
{
    const size_t begin = plan.streamIndices.size();
    plan.streamIndices.insert(
            plan.streamIndices.end(), outputs.begin(), outputs.end());
    plan.codecRuns.at(codecRun).outputs = {
        .begin = begin,
        .count = outputs.size(),
    };
}

/// Lists @p input as the only input of @p codecRun and records the consumer.
void consume(
        DecodePlan& plan,
        PlannedCodecRunIndex codecRun,
        PlannedStreamIndex input)
{
    plan.codecRuns.at(codecRun).inputs = {
        .begin = plan.streamIndices.size(),
        .count = 1,
    };
    plan.streamIndices.push_back(input);
    plan.streams.at(input).consumerCodecRun = codecRun;
}

/// Adds a chunk holding every stream added since the previous chunk.
void setChunk(DecodePlan& plan, size_t numOutputs)
{
    const size_t begin = plan.chunks.empty() ? 0
                                             : plan.chunks.back().streams.begin
                    + plan.chunks.back().streams.count;
    plan.chunks.push_back(
            {
                    .streams = {
                            .begin = begin,
                            .count = plan.streams.size() - begin,
                    },
                    .numOutputs = numOutputs,
            });
}

/// @returns The storage requirements of @p plan. Throws if derivation fails.
ExecutionRequirements storageRequirementsOrThrow(const DecodePlan& plan)
{
    Result<ExecutionRequirements> result = buildDecodeStorageRequirements(plan);
    if (result.isError()) {
        throw std::runtime_error(ZL_E_str(ZL_RES_error(result.report())));
    }
    return std::move(result).value();
}

DecodePlan makeChainPlan()
{
    DecodePlan plan;
    const PlannedStreamIndex source =
            addStream(plan, StorageClass::Stored, std::nullopt, 300);
    const PlannedCodecRunIndex first = addCodecRun(plan, {});
    const PlannedStreamIndex middle =
            addStream(plan, StorageClass::StreamArena, first, 100);
    setOutputs(plan, first, { middle });

    const PlannedCodecRunIndex second = addCodecRun(plan, { first });
    const PlannedStreamIndex final =
            addStream(plan, StorageClass::Destination, second, 400, 4);
    setOutputs(plan, second, { final });

    consume(plan, first, source);
    consume(plan, second, middle);
    setChunk(plan, 1);
    return plan;
}

TEST(StorageRequirementsTest, IncludesOnlyMaterializedStorageRegions)
{
    // This fails if frame-resident or alias-backed data receives an allocation,
    // or if materialized destination and workspace data is omitted.
    DecodePlan plan;
    const PlannedStreamIndex source =
            addStream(plan, StorageClass::Stored, std::nullopt, 300);
    const PlannedCodecRunIndex first = addCodecRun(plan, {});
    const PlannedStreamIndex middle =
            addStream(plan, StorageClass::StreamArena, first, 100);
    setOutputs(plan, first, { middle });

    const PlannedCodecRunIndex view = addCodecRun(plan, { first });
    const PlannedStreamIndex viewOutput =
            addStream(plan, StorageClass::StreamRef, view, 100, 1, 0, middle);
    setOutputs(plan, view, { viewOutput });

    const PlannedCodecRunIndex finalRun = addCodecRun(plan, { first });
    const PlannedStreamIndex final =
            addStream(plan, StorageClass::Destination, finalRun, 400, 4);
    setOutputs(plan, finalRun, { final });

    consume(plan, first, source);
    consume(plan, view, middle);
    setChunk(plan, 1);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    EXPECT_EQ(requirements.requirements.at(0).arena, ArenaClass::Destination);
    EXPECT_EQ(requirements.requirements.at(0).stream, final);
    EXPECT_EQ(requirements.requirements.at(1).arena, ArenaClass::StreamArena);
    EXPECT_EQ(requirements.requirements.at(1).stream, middle);
}

TEST(StorageRequirementsTest, BoundsWorkspaceLifetimeByProducerAndConsumer)
{
    // This fails if workspace can be reclaimed before its consumer codec run
    // or remains live after that final use without reason.
    const DecodePlan plan                    = makeChainPlan();
    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    const StorageLifetime expected{
        .firstUse = DecodeExecutionStage::codecRun(0),
        .lastUse  = DecodeExecutionStage::codecRun(1),
    };
    EXPECT_EQ(requirements.requirements.at(1).lifetime, expected);
}

TEST(StorageRequirementsTest, ExtendsBackingLifetimeThroughAlias)
{
    // This fails if an alias does not keep its backing storage alive until the
    // alias's consumer codec run finishes reading it.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex backing =
            addStream(plan, StorageClass::StreamArena, source, 100);
    setOutputs(plan, source, { backing });

    const PlannedCodecRunIndex view = addCodecRun(plan, { source });
    const PlannedStreamIndex alias =
            addStream(plan, StorageClass::StreamRef, view, 100, 1, 0, backing);
    setOutputs(plan, view, { alias });

    const PlannedCodecRunIndex consumer = addCodecRun(plan, { view });
    const PlannedStreamIndex consumed =
            addStream(plan, StorageClass::StreamArena, consumer, 100);
    setOutputs(plan, consumer, { consumed });

    consume(plan, view, backing);
    consume(plan, consumer, alias);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    const StorageLifetime expected{
        .firstUse = DecodeExecutionStage::codecRun(source),
        .lastUse  = DecodeExecutionStage::codecRun(consumer),
    };
    EXPECT_EQ(requirements.requirements.at(0).lifetime, expected);
}

TEST(StorageRequirementsTest, KeepsBackingThroughIndependentAliasConsumers)
{
    // This fails if independent alias consumers are treated as sequential,
    // which would let an allocator reuse their shared backing while one of
    // them still reads it.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex backing =
            addStream(plan, StorageClass::StreamArena, source, 100);
    setOutputs(plan, source, { backing });

    const PlannedCodecRunIndex view = addCodecRun(plan, { source });
    const PlannedStreamIndex firstAlias =
            addStream(plan, StorageClass::StreamRef, view, 100, 1, 0, backing);
    const PlannedStreamIndex secondAlias =
            addStream(plan, StorageClass::StreamRef, view, 100, 1, 0, backing);
    setOutputs(plan, view, { firstAlias, secondAlias });

    const PlannedCodecRunIndex firstConsumer  = addCodecRun(plan, { view });
    const PlannedCodecRunIndex secondConsumer = addCodecRun(plan, { view });
    consume(plan, view, backing);
    consume(plan, firstConsumer, firstAlias);
    consume(plan, secondConsumer, secondAlias);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 1);
    const StorageLifetime expected{
        .firstUse = DecodeExecutionStage::codecRun(source),
        .lastUse  = DecodeExecutionStage::decodeEnd(),
    };
    EXPECT_EQ(requirements.requirements.at(0).lifetime, expected);
}

TEST(StorageRequirementsTest, KeepsDestinationForTheWholeDecode)
{
    // This fails if a final destination is considered reclaimable at any point
    // while decoding may still write or expose the caller's result.
    const ExecutionRequirements requirements =
            storageRequirementsOrThrow(makeChainPlan());

    const StorageLifetime expected{
        .firstUse = DecodeExecutionStage::decodeStart(),
        .lastUse  = DecodeExecutionStage::decodeEnd(),
    };
    EXPECT_EQ(requirements.requirements.at(0).lifetime, expected);
}

TEST(StorageRequirementsTest, PreservesIndependentCodecRunLifetimes)
{
    // This fails if independent branches collapse onto one ordinal, which
    // would hide from an allocator that their storage may overlap.
    DecodePlan plan;
    const PlannedCodecRunIndex first = addCodecRun(plan, {});
    setOutputs(
            plan,
            first,
            { addStream(plan, StorageClass::StreamArena, first, 100) });
    const PlannedCodecRunIndex second = addCodecRun(plan, {});
    setOutputs(
            plan,
            second,
            { addStream(plan, StorageClass::StreamArena, second, 100) });
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    EXPECT_EQ(
            requirements.requirements.at(0).lifetime.firstUse,
            DecodeExecutionStage::codecRun(first));
    EXPECT_EQ(
            requirements.requirements.at(0).lifetime.lastUse,
            DecodeExecutionStage::codecRun(first));
    EXPECT_EQ(
            requirements.requirements.at(1).lifetime.firstUse,
            DecodeExecutionStage::codecRun(second));
    EXPECT_EQ(
            requirements.requirements.at(1).lifetime.lastUse,
            DecodeExecutionStage::codecRun(second));
}

TEST(StorageRequirementsTest, DerivesDataAlignmentFromElementWidth)
{
    // This fails if fixed-width data can be placed at an incompatible address,
    // if a non-power-of-two width gains an alignment it does not require, or
    // if a wide power-of-two width is not capped at 16 bytes.
    DecodePlan plan;
    const PlannedCodecRunIndex codecRun = addCodecRun(plan, {});
    const std::vector<PlannedStreamIndex> streams{
        addStream(plan, StorageClass::StreamArena, codecRun, 400, 4),
        addStream(plan, StorageClass::StreamArena, codecRun, 300, 3),
        addStream(plan, StorageClass::StreamArena, codecRun, 400, 20),
        addStream(plan, StorageClass::StreamArena, codecRun, 640, 32),
    };
    setOutputs(plan, codecRun, streams);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    std::vector<size_t> alignments;
    alignments.reserve(requirements.requirements.size());
    for (const StorageRequirement& requirement : requirements.requirements) {
        alignments.push_back(requirement.alignment);
    }
    const std::vector<size_t> expected{ 4, 1, 1, 16 };
    EXPECT_EQ(alignments, expected);
}

TEST(StorageRequirementsTest, AlignsWorkspaceForTheAliasesThatReadIt)
{
    // This fails if an owner keeps only its own alignment, which lets a wider
    // alias of it, such as a numeric view of serial bytes, start misaligned.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex bytes =
            addStream(plan, StorageClass::StreamArena, source, 400, 1);
    setOutputs(plan, source, { bytes });

    const PlannedCodecRunIndex view = addCodecRun(plan, { source });
    const PlannedStreamIndex numbers =
            addStream(plan, StorageClass::StreamRef, view, 400, 4, 0, bytes);
    setOutputs(plan, view, { numbers });
    consume(plan, view, bytes);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 1);
    EXPECT_EQ(requirements.requirements.at(0).stream, bytes);
    EXPECT_EQ(requirements.requirements.at(0).alignment, alignof(uint32_t));
}

TEST(StorageRequirementsTest, ExtendsTheOwnerThroughAnAliasChain)
{
    // This fails if a chained alias extends only its direct backing stream,
    // which would release the owner before the last alias is read.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex owner =
            addStream(plan, StorageClass::StreamArena, source, 100);
    setOutputs(plan, source, { owner });

    const PlannedCodecRunIndex firstView = addCodecRun(plan, { source });
    const PlannedStreamIndex firstAlias  = addStream(
            plan, StorageClass::StreamRef, firstView, 100, 1, 0, owner);
    setOutputs(plan, firstView, { firstAlias });

    const PlannedCodecRunIndex secondView = addCodecRun(plan, { firstView });
    const PlannedStreamIndex secondAlias  = addStream(
            plan, StorageClass::StreamRef, secondView, 100, 1, 0, firstAlias);
    setOutputs(plan, secondView, { secondAlias });

    const PlannedCodecRunIndex consumer = addCodecRun(plan, { secondView });
    setOutputs(
            plan,
            consumer,
            { addStream(plan, StorageClass::StreamArena, consumer, 100) });
    consume(plan, firstView, owner);
    consume(plan, secondView, firstAlias);
    consume(plan, consumer, secondAlias);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    EXPECT_EQ(requirements.requirements.at(0).stream, owner);
    const StorageLifetime expected{
        .firstUse = DecodeExecutionStage::codecRun(source),
        .lastUse  = DecodeExecutionStage::codecRun(consumer),
    };
    EXPECT_EQ(requirements.requirements.at(0).lifetime, expected);
}

TEST(StorageRequirementsTest, GivesNoWorkspaceToAnAliasOfAStoredStream)
{
    // This fails if an alias of frame-resident bytes gets workspace, or is
    // rejected, although its data already lives in the frame.
    DecodePlan plan;
    const PlannedStreamIndex stored =
            addStream(plan, StorageClass::Stored, std::nullopt, 100);
    const PlannedCodecRunIndex view = addCodecRun(plan, {});
    const PlannedStreamIndex alias =
            addStream(plan, StorageClass::StreamRef, view, 100, 1, 0, stored);
    setOutputs(plan, view, { alias });

    const PlannedCodecRunIndex consumer = addCodecRun(plan, { view });
    const PlannedStreamIndex output =
            addStream(plan, StorageClass::StreamArena, consumer, 100);
    setOutputs(plan, consumer, { output });
    consume(plan, view, stored);
    consume(plan, consumer, alias);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 1);
    EXPECT_EQ(requirements.requirements.at(0).stream, output);
}

TEST(StorageRequirementsTest, PlacesStringLengthsSeparately)
{
    // This fails if a string-length table is folded into the data region and
    // therefore loses its independent size or uint32_t alignment.
    DecodePlan plan;
    const PlannedCodecRunIndex codecRun = addCodecRun(plan, {});
    const PlannedStreamIndex text       = addStream(
            plan,
            StorageClass::StreamArena,
            codecRun,
            90,
            1,
            40,
            std::nullopt,
            ZL_Type_string);
    setOutputs(plan, codecRun, { text });
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    EXPECT_EQ(requirements.requirements.at(0).region, StreamRegion::Data);
    EXPECT_EQ(
            requirements.requirements.at(1).region,
            StreamRegion::StringLengths);
    EXPECT_EQ(requirements.requirements.at(1).size, 40);
    EXPECT_EQ(requirements.requirements.at(1).alignment, alignof(uint32_t));
}

TEST(StorageRequirementsTest, PlacesLengthsForAliasedStringStream)
{
    // This fails if aliasing the data region also suppresses the independently
    // materialized string-length table.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex backing =
            addStream(plan, StorageClass::StreamArena, source, 90);
    setOutputs(plan, source, { backing });

    const PlannedCodecRunIndex view = addCodecRun(plan, { source });
    const PlannedStreamIndex alias  = addStream(
            plan,
            StorageClass::StreamRef,
            view,
            90,
            1,
            40,
            backing,
            ZL_Type_string);
    setOutputs(plan, view, { alias });
    consume(plan, view, backing);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    EXPECT_EQ(requirements.requirements.at(0).stream, backing);
    EXPECT_EQ(requirements.requirements.at(1).stream, alias);
    EXPECT_EQ(
            requirements.requirements.at(1).region,
            StreamRegion::StringLengths);
}

TEST(StorageRequirementsTest, CreatesDestinationForStoredFinalOutput)
{
    // This fails if a frame-resident final output is mistaken for having no
    // destination merely because the executor must copy it after decoding.
    DecodePlan plan;
    const PlannedStreamIndex output =
            addStream(plan, StorageClass::Stored, std::nullopt, 300);
    setChunk(plan, 1);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 1);
    EXPECT_EQ(requirements.requirements.at(0).arena, ArenaClass::Destination);
    EXPECT_EQ(requirements.requirements.at(0).stream, output);
    EXPECT_EQ(requirements.requirements.at(0).size, 300);
}

TEST(StorageRequirementsTest, OrdersDestinationsByFinalOutputThenChunk)
{
    // This fails if destination regions follow chunk order across outputs,
    // which does not match core, where each final output is its own buffer
    // that receives each chunk's contribution in chunk order.
    DecodePlan plan;
    const PlannedStreamIndex chunk0Output1 =
            addStream(plan, StorageClass::Stored, std::nullopt, 10);
    const PlannedStreamIndex chunk0Output0 =
            addStream(plan, StorageClass::Stored, std::nullopt, 20);
    setChunk(plan, 2);
    const PlannedStreamIndex chunk1Output1 =
            addStream(plan, StorageClass::Stored, std::nullopt, 30);
    const PlannedStreamIndex chunk1Output0 =
            addStream(plan, StorageClass::Stored, std::nullopt, 40);
    setChunk(plan, 2);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    std::vector<std::pair<PlannedStreamIndex, std::optional<size_t>>> actual;
    actual.reserve(requirements.requirements.size());
    for (const StorageRequirement& requirement : requirements.requirements) {
        actual.emplace_back(requirement.stream, requirement.finalOutput);
    }
    const std::vector<std::pair<PlannedStreamIndex, std::optional<size_t>>>
            expected{
                { chunk0Output0, 0 },
                { chunk1Output0, 0 },
                { chunk0Output1, 1 },
                { chunk1Output1, 1 },
            };
    EXPECT_EQ(actual, expected);
}

TEST(StorageRequirementsTest, TiesStringLengthsToTheirFinalOutput)
{
    // This fails if a final string output's lengths lose their output, which
    // a layout needs to place them in that output's separate lengths buffer.
    DecodePlan plan;
    const PlannedStreamIndex output = addStream(
            plan,
            StorageClass::Stored,
            std::nullopt,
            90,
            1,
            40,
            std::nullopt,
            ZL_Type_string);
    setChunk(plan, 1);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    const StorageRequirement& lengths = requirements.requirements.at(1);
    EXPECT_EQ(lengths.stream, output);
    EXPECT_EQ(lengths.region, StreamRegion::StringLengths);
    EXPECT_EQ(lengths.arena, ArenaClass::Destination);
    EXPECT_EQ(lengths.finalOutput, 0);
}

TEST(StorageRequirementsTest, UsesCoreAlignmentForDestinationData)
{
    // This fails if a final output asks for more alignment than core requires
    // of a caller's buffer: only numeric outputs are aligned to their width.
    DecodePlan plan;
    addStream(
            plan,
            StorageClass::Stored,
            std::nullopt,
            400,
            4,
            0,
            std::nullopt,
            ZL_Type_struct);
    addStream(
            plan,
            StorageClass::Stored,
            std::nullopt,
            400,
            4,
            0,
            std::nullopt,
            ZL_Type_numeric);
    setChunk(plan, 2);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    EXPECT_EQ(requirements.requirements.at(0).alignment, alignof(uint32_t));
    EXPECT_EQ(requirements.requirements.at(1).alignment, 1);
}

TEST(StorageRequirementsTest, KeepsOwnerLengthTableLifetimeWithoutAliases)
{
    // This fails if an alias extends its owner's string-length table, which
    // the alias never reads, along with the owner's data.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex owner    = addStream(
            plan,
            StorageClass::StreamArena,
            source,
            90,
            1,
            40,
            std::nullopt,
            ZL_Type_string);
    setOutputs(plan, source, { owner });

    const PlannedCodecRunIndex view = addCodecRun(plan, { source });
    const PlannedStreamIndex alias =
            addStream(plan, StorageClass::StreamRef, view, 90, 1, 0, owner);
    setOutputs(plan, view, { alias });
    consume(plan, view, owner);

    const PlannedCodecRunIndex consumer = addCodecRun(plan, { view });
    setOutputs(
            plan,
            consumer,
            { addStream(plan, StorageClass::StreamArena, consumer, 90) });
    consume(plan, consumer, alias);
    setChunk(plan, 0);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_GE(requirements.requirements.size(), 2);
    const StorageRequirement& data    = requirements.requirements.at(0);
    const StorageRequirement& lengths = requirements.requirements.at(1);
    ASSERT_EQ(data.region, StreamRegion::Data);
    ASSERT_EQ(lengths.region, StreamRegion::StringLengths);
    EXPECT_EQ(data.lifetime.lastUse, DecodeExecutionStage::codecRun(consumer));
    EXPECT_EQ(lengths.lifetime.lastUse, DecodeExecutionStage::codecRun(view));
}

TEST(StorageRequirementsTest, WritesFinalAliasLengthsToTheDestination)
{
    // This fails if a final string alias also gets a workspace length table,
    // which would hold memory for the whole decode only to be copied into the
    // destination length table at the end.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex owner =
            addStream(plan, StorageClass::StreamArena, source, 90);
    setOutputs(plan, source, { owner });

    const PlannedCodecRunIndex view = addCodecRun(plan, { source });
    const PlannedStreamIndex output = addStream(
            plan,
            StorageClass::StreamRef,
            view,
            90,
            1,
            40,
            owner,
            ZL_Type_string);
    setOutputs(plan, view, { output });
    consume(plan, view, owner);
    setChunk(plan, 1);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    for (const StorageRequirement& requirement : requirements.requirements) {
        EXPECT_FALSE(
                requirement.stream == output
                && requirement.arena == ArenaClass::StreamArena)
                << "final alias has a workspace region";
    }
    ASSERT_EQ(requirements.requirements.size(), 3);
    EXPECT_EQ(
            requirements.requirements.at(1).region,
            StreamRegion::StringLengths);
    EXPECT_EQ(requirements.requirements.at(1).arena, ArenaClass::Destination);
}

TEST(StorageRequirementsTest, KeepsReferencedFinalOutputBackingUntilDecodeEnd)
{
    // This fails if terminal-copy source storage can be reused before the
    // executor copies a referenced final output into its destination.
    DecodePlan plan;
    const PlannedCodecRunIndex source = addCodecRun(plan, {});
    const PlannedStreamIndex backing =
            addStream(plan, StorageClass::StreamArena, source, 100);
    setOutputs(plan, source, { backing });

    const PlannedCodecRunIndex view = addCodecRun(plan, { source });
    const PlannedStreamIndex output =
            addStream(plan, StorageClass::StreamRef, view, 100, 1, 0, backing);
    setOutputs(plan, view, { output });
    consume(plan, view, backing);
    setChunk(plan, 1);

    const ExecutionRequirements requirements = storageRequirementsOrThrow(plan);

    ASSERT_EQ(requirements.requirements.size(), 2);
    EXPECT_EQ(requirements.requirements.at(0).arena, ArenaClass::Destination);
    EXPECT_EQ(requirements.requirements.at(1).stream, backing);
    const StorageLifetime expected{
        .firstUse = DecodeExecutionStage::codecRun(source),
        .lastUse  = DecodeExecutionStage::decodeEnd(),
    };
    EXPECT_EQ(requirements.requirements.at(1).lifetime, expected);
}

} // namespace
} // namespace openzl::gpu
