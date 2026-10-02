// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {buildSeries, formatRatioTick, formatSpeedTick, logDomain, logTicks} from '../src/charts.ts';
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

describe('buildSeries', () => {
  it('draws one line per codec, not per compressor row', () => {
    // Two OpenZL rows share a colour, so two lines would be indistinguishable.
    const series = buildSeries(
      [
        result({compressor: 'OpenZL', rowId: 1}),
        result({compressor: 'OpenZL', rowId: 4}),
        result({compressor: 'zstd', rowId: 2}),
      ],
      'compressMBps',
    );

    expect(series.map((one) => one.compressor)).toEqual(['OpenZL', 'zstd']);
    expect(series[0].points).toHaveLength(2);
  });

  it('orders points by speed so the line does not double back', () => {
    const [series] = buildSeries(
      [
        result({compressor: 'zstd', level: 1}, {compressMBps: 500, ratio: 3}),
        result({compressor: 'zstd', level: 9}, {compressMBps: 90, ratio: 7}),
      ],
      'compressMBps',
    );
    expect(series.points.map((point) => point.x)).toEqual([90, 500]);
  });

  it('plots the axis it was asked for', () => {
    const [byDecompress] = buildSeries([result({compressor: 'gzip'}, {decompressMBps: 400})], 'decompressMBps');
    expect(byDecompress.points[0].x).toBe(400);
  });

  it('drops what a log axis cannot place, as the domain does', () => {
    // An empty input measures as 0 MB/s at a ratio of 0. `logDomain` already
    // leaves it out of the range, so a point at log(0) would have nowhere
    // on the frame to go.
    const series = buildSeries(
      [
        result({compressor: 'zstd', level: 1}, {compressMBps: 0, ratio: 0}),
        result({compressor: 'zstd', level: 9}, {compressMBps: 90, ratio: 7}),
      ],
      'compressMBps',
    );
    expect(series[0].points.map((point) => point.x)).toEqual([90]);
  });

  it('names a point the way the table does', () => {
    const [trained] = buildSeries([result({compressor: 'OpenZL'}, {candidate: {index: 2, total: 5}})], 'compressMBps');
    const [levelled] = buildSeries([result({compressor: 'zstd', level: 9})], 'compressMBps');
    expect(trained.points[0].label).toBe('#2');
    expect(levelled.points[0].label).toBe('Level 9');
  });
});

describe('logDomain', () => {
  it('leaves margin either side so points are not on the frame', () => {
    const [low, high] = logDomain([100, 500]);
    expect(low).toBeLessThan(100);
    expect(high).toBeGreaterThan(500);
  });

  it('gives a single point a domain it can sit inside', () => {
    // Identical ends make a zero-width scale, which cannot be inverted.
    const [low, high] = logDomain([42, 42]);
    expect(low).toBeLessThan(42);
    expect(high).toBeGreaterThan(42);
  });

  it('ignores values a log axis cannot place', () => {
    expect(logDomain([0, -5, 200])).toEqual(logDomain([200]));
    expect(logDomain([])).toEqual([1, 10]);
  });
});

describe('logTicks', () => {
  it('labels the powers of ten inside the domain', () => {
    expect(logTicks([224 / 1.3, 15235 * 1.3])).toEqual([1000, 10000]);
    // The design's speed axis carries exactly one label, so one is enough.
    expect(logTicks([38 / 1.3, 520 * 1.3])).toEqual([100]);
  });

  it('picks round numbers when less than a decade is on screen', () => {
    // Real zstd and gzip ratios land here, and the domain's padded ends make
    // labels like `3.46×` and `6.99×` that nobody reads off an axis.
    expect(logTicks([3.46, 6.99])).toEqual([4, 5, 6]);
    expect(logTicks([3.5, 9.6])).toEqual([4, 6, 8]);
  });

  it('labels something however narrow the range, without float drift', () => {
    // A step accumulated as a float would land on 5.029999999999999 here.
    expect(logTicks([5.02, 5.04])).toEqual([5.02, 5.03, 5.04]);
  });
});

describe('tick formatting', () => {
  it('shortens a ratio to what fits beside the axis', () => {
    expect(formatRatioTick(1000)).toBe('1.0k×');
    expect(formatRatioTick(10000)).toBe('10k×');
    expect(formatRatioTick(342)).toBe('342×');
    expect(formatRatioTick(5.4)).toBe('5.4×');
  });

  it('leaves the unit to the axis title', () => {
    expect(formatSpeedTick(100)).toBe('100');
    expect(formatSpeedTick(38.25)).toBe('38.3');
  });
});
