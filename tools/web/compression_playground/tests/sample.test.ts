// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {gzipSync} from 'node:zlib';
import {describe, expect, it} from 'vitest';
import {makeSample} from '../src/sample.ts';

describe('makeSample', () => {
  it('is a 5 MB text file by default', () => {
    const sample = makeSample();

    expect(sample.name).toBe('sample.txt');
    expect(sample.type).toBe('text/plain');
    expect(sample.size).toBe(5_000_000);
  });

  it('honours the size it is asked for, even mid-word', () => {
    expect(makeSample(100).size).toBe(100);
    expect(makeSample(1).size).toBe(1);
  });

  it('is lorem ipsum', async () => {
    const text = await makeSample(2000).text();

    expect(text.startsWith('Lorem ipsum ')).toBe(true);
    expect(text).toMatch(/^[A-Za-z ]+$/);
  });

  it('is the same bytes every time, so runs of the tool are comparable', async () => {
    const [a, b] = await Promise.all([makeSample(10_000).text(), makeSample(10_000).text()]);

    expect(a).toBe(b);
  });

  it('is not one line repeated, which would compress to nothing', async () => {
    // A repeated line makes every ratio on the page enormous. Drawn words
    // compress like ordinary prose.
    const bytes = new Uint8Array(await makeSample(1_000_000).arrayBuffer());
    const ratio = bytes.length / gzipSync(bytes).length;

    expect(ratio).toBeGreaterThan(1.5);
    expect(ratio).toBeLessThan(10);
  });
});
