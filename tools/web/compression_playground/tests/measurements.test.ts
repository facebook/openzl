// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {
  barFraction,
  formatBytes,
  formatRatio,
  formatRatioRange,
  formatSpeedRange,
  frontierTag,
  groupMeasurements,
  peaksOf,
  rowDetail,
  sortGroups,
} from '../src/measurements.ts';
import type {BenchmarkJob, JobResult} from '../src/benchmarkTypes.ts';

function result(
  job: Partial<BenchmarkJob> & Pick<BenchmarkJob, 'compressor'>,
  metrics: Partial<JobResult> = {},
): JobResult {
  return {
    iterations: 1,
    srcSize: 1000,
    compressedSize: 100,
    ratio: 10,
    compressMs: 1,
    decompressMs: 1,
    compressMBps: 100,
    decompressMBps: 200,
    candidate: null,
    ...metrics,
    job: {id: 'j', rowId: 1, level: 1, iterations: 1, profile: 0, training: null, ...job} as BenchmarkJob,
  };
}

describe('frontierTag', () => {
  it('names only the ends and the middle of a frontier', () => {
    expect([1, 2, 3, 4, 5].map((i) => frontierTag(i, 5))).toEqual(['max ratio', null, 'balanced', null, 'max speed']);
    expect([1, 2, 3, 4, 5, 6].map((i) => frontierTag(i, 6))).toEqual([
      'max ratio',
      null,
      'balanced',
      null,
      null,
      'max speed',
    ]);
  });

  it('leaves a single measurement unlabelled and a pair without a middle', () => {
    // One point has nothing to be the extreme of, and two are both extremes.
    expect(frontierTag(1, 1)).toBeNull();
    expect([1, 2].map((i) => frontierTag(i, 2))).toEqual(['max ratio', 'max speed']);
  });
});

describe('groupMeasurements', () => {
  it('groups by compressor row, keeping configuration order', () => {
    const groups = groupMeasurements([
      result({compressor: 'zstd', rowId: 2, level: 1}),
      result({compressor: 'OpenZL', rowId: 1}),
      result({compressor: 'zstd', rowId: 2, level: 5}),
    ]);

    expect(groups.map((group) => group.rowId)).toEqual([2, 1]);
    expect(groups[0].rows).toHaveLength(2);
  });

  it('describes a group by what produced its rows', () => {
    const trained = groupMeasurements([
      result({compressor: 'OpenZL', rowId: 1, profile: 0}, {candidate: {index: 1, total: 2}}),
      result({compressor: 'OpenZL', rowId: 1, profile: 0}, {candidate: {index: 2, total: 2}}),
    ]);
    expect(trained[0].detail).toBe('trained · serial');

    const untrained = groupMeasurements([result({compressor: 'OpenZL', rowId: 1, profile: 5})]);
    expect(untrained[0].detail).toBe('le-u32');

    const levels = groupMeasurements([
      result({compressor: 'gzip', rowId: 3, level: 1}),
      result({compressor: 'gzip', rowId: 3, level: 9}),
    ]);
    expect(levels[0].detail).toBe('2 levels');
  });
});

describe('sortGroups', () => {
  it('orders rows inside a group without moving rows between groups', () => {
    const groups = groupMeasurements([
      result({compressor: 'zstd', rowId: 2, level: 1}, {ratio: 3}),
      result({compressor: 'zstd', rowId: 2, level: 5}, {ratio: 9}),
      result({compressor: 'gzip', rowId: 3, level: 1}, {ratio: 5}),
    ]);

    const sorted = sortGroups(groups, 'ratio', 'desc');
    expect(sorted.map((group) => group.rowId)).toEqual([2, 3]);
    expect(sorted[0].rows.map((row) => row.ratio)).toEqual([9, 3]);
    expect(sortGroups(groups, 'ratio', 'asc')[0].rows.map((row) => row.ratio)).toEqual([3, 9]);
  });

  it('leaves the produced order alone when grouped by codec', () => {
    const groups = groupMeasurements([
      result({compressor: 'zstd', rowId: 2, level: 5}, {ratio: 9}),
      result({compressor: 'zstd', rowId: 2, level: 1}, {ratio: 3}),
    ]);
    expect(sortGroups(groups, 'codec', 'asc')[0].rows.map((row) => row.ratio)).toEqual([9, 3]);
  });
});

describe('formatting', () => {
  it('scales bytes the way the design writes them', () => {
    expect(formatBytes(361)).toBe('361 B');
    expect(formatBytes(1900)).toBe('1.9 KB');
    expect(formatBytes(1_000_000)).toBe('1.0 MB');
  });

  it('keeps two decimals on a ratio however large it is', () => {
    expect(formatRatio(15235.46)).toBe('15235.46×');
    expect(formatRatio(3.204)).toBe('3.20×');
  });

  it('drops the decimals from a ratio range and the unit from a speed range', () => {
    expect(formatRatioRange([2763.82, 15235.46])).toBe('2764× - 15235×');
    expect(formatSpeedRange([38.2, 310.4])).toBe('38 - 310 MB/s');
  });

  it('keeps the decimals when rounding would close a range', () => {
    // Real zstd and gzip ratios sit under 10, where `5× - 5×` reads as one
    // measurement rather than six.
    expect(formatRatioRange([4.51, 5.38])).toBe('4.51× - 5.38×');
  });

  it('collapses a range of one', () => {
    expect(formatRatioRange([5.5])).toBe('6×');
    expect(formatSpeedRange([38, 38])).toBe('38 MB/s');
  });
});

describe('rowDetail', () => {
  it('leaves a trained candidate without a level of its own', () => {
    // Every candidate shares the group's profile, which the group row carries.
    expect(rowDetail(result({compressor: 'OpenZL', profile: 5}, {candidate: {index: 1, total: 3}}))).toBe('-');
    expect(rowDetail(result({compressor: 'OpenZL', profile: 5}))).toBe('le-u32');
    expect(rowDetail(result({compressor: 'zstd', level: 9}))).toBe('Level 9');
  });
});

describe('barFraction', () => {
  it('measures against the whole run, so codecs stay comparable', () => {
    const peaks = peaksOf([result({compressor: 'zstd'}, {ratio: 10}), result({compressor: 'gzip'}, {ratio: 5})]);
    expect(peaks.ratio).toBe(10);
    expect(barFraction(10, peaks.ratio)).toBe(1);
    expect(barFraction(5, peaks.ratio)).toBe(0.5);
  });

  it('keeps a sliver visible and never overflows the track', () => {
    // A bar of zero width reads as a missing value rather than a small one.
    expect(barFraction(0.0001, 1000)).toBe(0.02);
    expect(barFraction(5, 1)).toBe(1);
    expect(barFraction(1, 0)).toBe(0);
  });
});
