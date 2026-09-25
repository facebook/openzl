// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {evenlySpacedLevels, levelsFor} from '../src/compressors.ts';

describe('evenlySpacedLevels', () => {
  it('spreads the asked-for count across the range', () => {
    expect(evenlySpacedLevels('zstd', 6)).toEqual([1, 5, 8, 12, 15, 19]);
    expect(evenlySpacedLevels('gzip', 6)).toEqual([1, 3, 4, 6, 7, 9]);
    expect(evenlySpacedLevels('zstd', 7)).toEqual([1, 4, 7, 10, 13, 16, 19]);
  });

  it('always includes both ends, so a curve spans the range', () => {
    for (const compressor of ['zstd', 'gzip'] as const) {
      const all = levelsFor(compressor);
      for (let count = 2; count <= all.length; count += 1) {
        const levels = evenlySpacedLevels(compressor, count);
        expect(levels[0]).toBe(all[0]);
        expect(levels[levels.length - 1]).toBe(all[all.length - 1]);
      }
    }
  });

  it('returns exactly the count asked for, up to what the range holds', () => {
    for (const compressor of ['zstd', 'gzip'] as const) {
      const all = levelsFor(compressor);
      for (let count = 1; count <= 30; count += 1) {
        const levels = evenlySpacedLevels(compressor, count);
        expect(levels).toHaveLength(Math.min(count, all.length));
        expect(new Set(levels).size).toBe(levels.length);
        expect(levels.every((level) => all.includes(level))).toBe(true);
      }
    }
  });

  it('clamps rather than padding once the range runs out', () => {
    // The trainer accepts up to 25 candidates; gzip has nine levels and zstd
    // nineteen, so past those the codecs simply stop matching.
    expect(evenlySpacedLevels('gzip', 25)).toEqual(levelsFor('gzip'));
    expect(evenlySpacedLevels('zstd', 25)).toEqual(levelsFor('zstd'));
  });
});
