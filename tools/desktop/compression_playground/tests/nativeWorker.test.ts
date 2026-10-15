// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {afterEach, describe, expect, it} from 'vitest';
import {run, type BenchmarkEvent} from '../electron/native-worker.cjs';
import {fakeZli, removeFakeZlis} from './fakeZli.ts';

afterEach(removeFakeZlis);

describe('run', () => {
  it('reports each row and stage as it starts, and each row’s results as it ends', async () => {
    const events: BenchmarkEvent[] = [];
    const results = await run(
      {
        zliPath: fakeZli(),
        request: {
          input: {data: new TextEncoder().encode('OpenZL '.repeat(1000))},
          iterations: 2,
          compressors: [
            {
              id: 1,
              compressor: 'OpenZL',
              level: 6,
              profile: 'serial',
              trainRequested: true,
              candidates: 10,
              profileArg: '',
            },
            {id: 2, compressor: 'gzip', level: 5},
          ],
        },
      },
      {onEvent: (event) => events.push(event)},
    );

    expect(
      events.map((event) =>
        event.type === 'progress' ? event.progress : {partial: event.results.map((r) => r.label)},
      ),
    ).toEqual([
      {rowId: 1, rowIndex: 0, rowCount: 2, label: 'OpenZL serial', stage: 'train'},
      {
        rowId: 1,
        rowIndex: 0,
        rowCount: 2,
        label: 'OpenZL serial',
        stage: 'candidates',
        candidate: 1,
        candidateCount: 1,
      },
      {partial: ['OpenZL serial']},
      {rowId: 2, rowIndex: 1, rowCount: 2, label: 'gzip 5', stage: 'benchmark'},
      {partial: ['gzip 5']},
    ]);
    expect(results.map((result) => result.label)).toEqual(['OpenZL serial', 'gzip 5']);
  });
});
