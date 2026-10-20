// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "contrib/gpu/src/scheduling/work_schedule.hpp"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_errors.h"

namespace openzl::gpu {
namespace {

constexpr ZL_IDType kCodecA = 1;
constexpr ZL_IDType kCodecB = 2;

PublicTransformInfo standardTransform(ZL_IDType trid)
{
    PublicTransformInfo transform{};
    transform.trt  = trt_standard;
    transform.trid = trid;
    return transform;
}

/// Appends a materialized stream and returns its dense index.
PlannedStreamIndex addStream(
        DecodePlan& plan,
        PlannedCodecRunIndex producerCodecRun)
{
    plan.streams.push_back(
            {
                    .storageClass     = StorageClass::StreamArena,
                    .producerCodecRun = producerCodecRun,
                    .aliasOffset      = 0,
            });
    return plan.streams.size() - 1;
}

/// Appends a stream backed by @p backing and returns its dense index.
PlannedStreamIndex addAliasedStream(
        DecodePlan& plan,
        PlannedCodecRunIndex producerCodecRun,
        PlannedStreamIndex backing,
        size_t stringLengthsBytes = 0)
{
    plan.streams.push_back(
            {
                    .storageSize = { .stringLengthsBytes = stringLengthsBytes },
                    .storageClass     = StorageClass::StreamRef,
                    .producerCodecRun = producerCodecRun,
                    .aliasStream      = backing,
                    .aliasOffset      = 0,
            });
    return plan.streams.size() - 1;
}

/// Appends a codec run and returns its dense index.
PlannedCodecRunIndex addCodecRun(
        DecodePlan& plan,
        ZL_IDType trid,
        const std::vector<PlannedCodecRunIndex>& dependencies)
{
    const size_t begin = plan.codecRunIndices.size();
    for (const PlannedCodecRunIndex dependency : dependencies) {
        plan.codecRunIndices.push_back(dependency);
    }
    plan.codecRuns.push_back(
            {
                    .transform    = standardTransform(trid),
                    .dependencies = { .begin = begin,
                                      .count = dependencies.size() },
            });
    return plan.codecRuns.size() - 1;
}

/// Binds a codec run's outputs. Inputs are unset because scheduling ignores
/// them.
void setOutputs(
        DecodePlan& plan,
        PlannedCodecRunIndex codecRun,
        const std::vector<PlannedStreamIndex>& outputs)
{
    const size_t begin = plan.streamIndices.size();
    for (const PlannedStreamIndex output : outputs) {
        plan.streamIndices.push_back(output);
    }
    plan.codecRuns[codecRun].outputs = {
        .begin = begin,
        .count = outputs.size(),
    };
}

/// @returns The work schedule for @p plan. Throws if scheduling fails.
WorkSchedule scheduleOrThrow(const DecodePlan& plan)
{
    Result<WorkSchedule> result = buildDecodeWorkSchedule(plan);
    if (result.isError()) {
        throw std::runtime_error(ZL_E_str(ZL_RES_error(result.report())));
    }
    return std::move(result).value();
}

ZL_ErrorCode scheduleErrorCode(const DecodePlan& plan)
{
    return ZL_errorCode(buildDecodeWorkSchedule(plan).report());
}

const std::vector<PlannedCodecRunIndex>& codecRunsOf(
        const WorkSchedule& schedule,
        size_t waveIndex,
        size_t workGroupIndex)
{
    return schedule.waves.at(waveIndex)
            .workGroups.at(workGroupIndex)
            .codecRunIndices;
}

const std::vector<WorkGroup>& workGroupsOf(
        const WorkSchedule& schedule,
        size_t waveIndex)
{
    return schedule.waves.at(waveIndex).workGroups;
}

TEST(WorkScheduleTest, AssignsWavesFromDependencyDepth)
{
    // This fails if a wave does not strictly exceed every producer it
    // depends on, which would let a consumer batch launch before its input
    // exists.
    DecodePlan plan;
    const PlannedCodecRunIndex first = addCodecRun(plan, kCodecA, {});
    setOutputs(plan, first, { addStream(plan, first) });
    const PlannedCodecRunIndex second = addCodecRun(plan, kCodecA, { first });
    setOutputs(plan, second, { addStream(plan, second) });
    const PlannedCodecRunIndex third = addCodecRun(plan, kCodecA, { second });
    setOutputs(plan, third, { addStream(plan, third) });

    const WorkSchedule schedule = scheduleOrThrow(plan);

    ASSERT_EQ(schedule.waves.size(), 3);
    ASSERT_EQ(workGroupsOf(schedule, 0).size(), 1);
    ASSERT_EQ(workGroupsOf(schedule, 1).size(), 1);
    ASSERT_EQ(workGroupsOf(schedule, 2).size(), 1);
    EXPECT_EQ(codecRunsOf(schedule, 0, 0), std::vector{ first });
    EXPECT_EQ(codecRunsOf(schedule, 1, 0), std::vector{ second });
    EXPECT_EQ(codecRunsOf(schedule, 2, 0), std::vector{ third });
}

TEST(WorkScheduleTest, BatchesIndependentCodecRunsOfOneTransform)
{
    // This fails if codec runs sharing a transform and wave are not collapsed
    // into one launch, which is the schedule's batching contract.
    DecodePlan plan;
    for (int chunk = 0; chunk < 3; ++chunk) {
        const PlannedCodecRunIndex codecRun = addCodecRun(plan, kCodecA, {});
        setOutputs(plan, codecRun, { addStream(plan, codecRun) });
    }

    const WorkSchedule schedule = scheduleOrThrow(plan);

    ASSERT_EQ(schedule.waves.size(), 1);
    ASSERT_EQ(workGroupsOf(schedule, 0).size(), 1);
    EXPECT_EQ(workGroupsOf(schedule, 0)[0].transform.trid, kCodecA);
    const std::vector<PlannedCodecRunIndex> expected{ 0, 1, 2 };
    EXPECT_EQ(codecRunsOf(schedule, 0, 0), expected);
}

TEST(WorkScheduleTest, SplitsWorkGroupsByWaveAndTransform)
{
    // This fails if codec runs from different transforms or waves share a
    // batch, launching a codec over incompatible or unavailable inputs.
    DecodePlan plan;
    const PlannedCodecRunIndex a0 = addCodecRun(plan, kCodecA, {});
    setOutputs(plan, a0, { addStream(plan, a0) });
    const PlannedCodecRunIndex b0 = addCodecRun(plan, kCodecB, {});
    setOutputs(plan, b0, { addStream(plan, b0) });
    const PlannedCodecRunIndex a1 = addCodecRun(plan, kCodecA, { a0 });
    setOutputs(plan, a1, { addStream(plan, a1) });

    const WorkSchedule schedule = scheduleOrThrow(plan);

    ASSERT_EQ(schedule.waves.size(), 2);
    const auto& wave0WorkGroups = workGroupsOf(schedule, 0);
    const auto& wave1WorkGroups = workGroupsOf(schedule, 1);
    ASSERT_EQ(wave0WorkGroups.size(), 2);
    ASSERT_EQ(wave1WorkGroups.size(), 1);
    EXPECT_EQ(wave0WorkGroups[0].transform.trid, kCodecA);
    EXPECT_EQ(wave0WorkGroups[1].transform.trid, kCodecB);
    EXPECT_EQ(wave1WorkGroups[0].transform.trid, kCodecA);
}

TEST(WorkScheduleTest, PreservesPlanOrderWithinOneWave)
{
    // This fails if scheduling imposes a transform priority on independent
    // work groups instead of retaining their order in the decode plan.
    DecodePlan plan;
    const PlannedCodecRunIndex b = addCodecRun(plan, kCodecB, {});
    setOutputs(plan, b, { addStream(plan, b) });
    const PlannedCodecRunIndex a = addCodecRun(plan, kCodecA, {});
    setOutputs(plan, a, { addStream(plan, a) });

    const WorkSchedule schedule = scheduleOrThrow(plan);

    ASSERT_EQ(schedule.waves.size(), 1);
    const auto& workGroups = workGroupsOf(schedule, 0);
    ASSERT_EQ(workGroups.size(), 2);
    EXPECT_EQ(workGroups[0].transform.trid, kCodecB);
    EXPECT_EQ(workGroups[1].transform.trid, kCodecA);
}

TEST(WorkScheduleTest, LaunchesNothingForAliasingCodecRuns)
{
    // This fails if a codec run that only reinterprets existing storage is
    // launched, or if it delays its consumer by a wave.
    DecodePlan plan;
    const PlannedCodecRunIndex source  = addCodecRun(plan, kCodecA, {});
    const PlannedStreamIndex sourceOut = addStream(plan, source);
    setOutputs(plan, source, { sourceOut });

    const PlannedCodecRunIndex view = addCodecRun(plan, kCodecB, { source });
    setOutputs(plan, view, { addAliasedStream(plan, view, sourceOut) });

    const PlannedCodecRunIndex consumer = addCodecRun(plan, kCodecA, { view });
    setOutputs(plan, consumer, { addStream(plan, consumer) });

    const WorkSchedule schedule = scheduleOrThrow(plan);

    ASSERT_EQ(schedule.waves.size(), 2);
    ASSERT_EQ(workGroupsOf(schedule, 0).size(), 1);
    EXPECT_EQ(schedule.aliasingCodecRunIndices, std::vector{ view });
    ASSERT_EQ(workGroupsOf(schedule, 1).size(), 1);
    EXPECT_EQ(
            codecRunsOf(schedule, 0, 0),
            std::vector<PlannedCodecRunIndex>{ source });
    EXPECT_EQ(
            codecRunsOf(schedule, 1, 0),
            std::vector<PlannedCodecRunIndex>{ consumer });
}

TEST(WorkScheduleTest, LaunchesAliasedOutputsThatStillNeedLengthStorage)
{
    // This fails if elision looks only at the storage class and ignores the
    // string length table, which an alias never covers.
    DecodePlan plan;
    const PlannedCodecRunIndex source  = addCodecRun(plan, kCodecA, {});
    const PlannedStreamIndex sourceOut = addStream(plan, source);
    setOutputs(plan, source, { sourceOut });

    const PlannedCodecRunIndex view = addCodecRun(plan, kCodecB, { source });
    setOutputs(plan, view, { addAliasedStream(plan, view, sourceOut, 64) });

    const WorkSchedule schedule = scheduleOrThrow(plan);

    ASSERT_EQ(schedule.waves.size(), 2);
    ASSERT_EQ(workGroupsOf(schedule, 1).size(), 1);
    EXPECT_EQ(
            codecRunsOf(schedule, 1, 0),
            std::vector<PlannedCodecRunIndex>{ view });
}

TEST(WorkScheduleTest, RejectsDependencyOnALaterCodecRun)
{
    // This fails if the scheduler trusts codec run order: a forward or cyclic
    // edge would otherwise read a wave that is not final.
    DecodePlan plan;
    const PlannedCodecRunIndex first = addCodecRun(plan, kCodecA, { 1 });
    setOutputs(plan, first, { addStream(plan, first) });
    const PlannedCodecRunIndex second = addCodecRun(plan, kCodecA, {});
    setOutputs(plan, second, { addStream(plan, second) });

    EXPECT_EQ(scheduleErrorCode(plan), ZL_ErrorCode_corruption);
}

TEST(WorkScheduleTest, RejectsDependencyRangePastIndices)
{
    // This fails if scheduling slices a dependency range without checking it.
    DecodePlan plan;
    const PlannedCodecRunIndex codecRun = addCodecRun(plan, kCodecA, {});
    setOutputs(plan, codecRun, { addStream(plan, codecRun) });
    plan.codecRuns[codecRun].dependencies = { .begin = 0, .count = 1 };
    EXPECT_EQ(scheduleErrorCode(plan), ZL_ErrorCode_corruption);
}

TEST(WorkScheduleTest, RejectsOutputRangePastIndices)
{
    // This fails if scheduling slices an output range without checking it.
    DecodePlan plan;
    const PlannedCodecRunIndex codecRun = addCodecRun(plan, kCodecA, {});
    setOutputs(plan, codecRun, { addStream(plan, codecRun) });
    plan.codecRuns[codecRun].outputs = { .begin = 0, .count = 2 };
    EXPECT_EQ(scheduleErrorCode(plan), ZL_ErrorCode_corruption);
}

TEST(WorkScheduleTest, RejectsOutputOfAnUnknownStream)
{
    // This fails if elision reads a stream index that the plan does not hold.
    DecodePlan plan;
    const PlannedCodecRunIndex codecRun = addCodecRun(plan, kCodecA, {});
    setOutputs(plan, codecRun, { plan.streams.size() });
    EXPECT_EQ(scheduleErrorCode(plan), ZL_ErrorCode_corruption);
}

TEST(WorkScheduleTest, SchedulesNothingForAnEmptyPlan)
{
    // This fails if an empty batch produces a work group, which submission
    // would then launch over zero streams.
    const WorkSchedule schedule = scheduleOrThrow(DecodePlan{});

    EXPECT_TRUE(schedule.waves.empty());
}

} // namespace
} // namespace openzl::gpu
