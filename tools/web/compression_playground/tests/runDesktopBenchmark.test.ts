// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import type {
  BenchmarkEvent,
  BenchmarkRequest,
  BenchmarkResult,
} from '../../../desktop/compression_playground/electron/native-worker.cjs';
import type {CompressorConfig, RunConfig, RunInput, RunState} from '../src/benchmarkTypes.ts';
import type {OpenZLDesktopBridge} from '../src/desktop.ts';
import {BUNDLED_SAMPLE} from '../src/sample.ts';

/**
 * A stand-in for the preload bridge. The test decides when the backend says
 * what, and when the run ends.
 */
function fakeBridge(path = '/tmp/data.bin') {
  const started: {
    request: BenchmarkRequest;
    emit: (event: BenchmarkEvent) => void;
    resolve: () => void;
    reject: (error: Error) => void;
    cancel: ReturnType<typeof vi.fn>;
  }[] = [];
  const bridge: OpenZLDesktopBridge = {
    getPathForFile: () => path,
    startBenchmark: (request, onEvent) => {
      const cancel = vi.fn();
      const done = new Promise<void>((resolve, reject) => {
        started.push({
          request,
          emit: (event) => onEvent?.(event),
          resolve: () => {
            resolve();
          },
          reject,
          cancel,
        });
      });
      return {done, cancel};
    },
  };
  return {bridge, started};
}

const OPENZL: CompressorConfig = {
  rowId: 1,
  compressor: 'OpenZL',
  levels: [6],
  profile: 'serial',
  profileArg: '',
  training: null,
};

function configWith(
  compressors: readonly CompressorConfig[],
  input: RunInput = new File(['x'], 'data.bin'),
): RunConfig {
  return {input, compressors, iterations: 3};
}

function metrics(overrides: Partial<BenchmarkResult> = {}): BenchmarkResult {
  return {
    rowId: 0,
    candidate: null,
    iterations: 3,
    srcSize: 1000,
    compressedSize: 250,
    ratio: 4,
    compressMs: 10,
    decompressMs: 5,
    compressMBps: 100,
    decompressMBps: 200,
    ...overrides,
  };
}

/** Lets the input resolve and the run start, which takes a few microtasks. */
const settle = () => vi.advanceTimersByTimeAsync(0);

// Fresh each time: the executor refuses a second run while one is in flight,
// and that is module state a test must not inherit from the one before.
let runDesktopBenchmark: typeof import('../src/runDesktopBenchmark.ts').runDesktopBenchmark;

function run(config: RunConfig, bridge: OpenZLDesktopBridge) {
  const states: RunState[] = [];
  runDesktopBenchmark(config, bridge, (state) => states.push(state));
  return {states, last: () => states[states.length - 1]};
}

beforeEach(async () => {
  vi.useFakeTimers();
  vi.resetModules();
  ({runDesktopBenchmark} = await import('../src/runDesktopBenchmark.ts'));
});

afterEach(() => {
  vi.useRealTimers();
});

describe('runDesktopBenchmark', () => {
  it('reports itself busy before the input has even been read', () => {
    // The run button disables off this state, and reading a large file can
    // take a while.
    const {bridge} = fakeBridge();
    const {states} = run(configWith([OPENZL]), bridge);

    expect(states).toEqual([{status: 'loading'}]);
  });

  it('sends one row per compressor per level, numbered by position', async () => {
    const {bridge, started} = fakeBridge('/tmp/data.bin');
    const {last} = run(
      configWith([OPENZL, {rowId: 2, compressor: 'zstd', levels: [3, 9]}, {rowId: 3, compressor: 'gzip', levels: [5]}]),
      bridge,
    );
    await settle();

    expect(started[0].request).toEqual({
      input: {path: '/tmp/data.bin'},
      iterations: 3,
      compressors: [
        // Untrained, so there is no candidate count to send.
        {id: 0, compressor: 'OpenZL', level: 6, profile: 'serial', trainRequested: false, profileArg: ''},
        {id: 1, compressor: 'zstd', level: 3},
        {id: 2, compressor: 'zstd', level: 9},
        {id: 3, compressor: 'gzip', level: 5},
      ],
    });
    expect(last()).toMatchObject({status: 'running', completedJobs: 0, totalJobs: 4, step: null});
    started[0].resolve();
    await settle();
  });

  it('asks for training, and passes a profile only the CLI has, with its argument', async () => {
    const {bridge, started} = fakeBridge();
    run(
      configWith([
        {
          ...OPENZL,
          profile: 'csv',
          profileArg: ';',
          training: {paretoFrontier: true, candidates: 12},
        },
      ]),
      bridge,
    );
    await settle();

    expect(started[0].request.compressors).toEqual([
      {id: 0, compressor: 'OpenZL', level: 6, profile: 'csv', trainRequested: true, candidates: 12, profileArg: ';'},
    ]);
    started[0].resolve();
    await settle();
  });

  it('counts a job done once its last measurement lands, and keeps the candidates the backend numbered', async () => {
    const {bridge, started} = fakeBridge();
    const {last} = run(
      configWith([
        {rowId: 2, compressor: 'gzip', levels: [5]},
        {...OPENZL, training: {paretoFrontier: true, candidates: 10}},
      ]),
      bridge,
    );
    await settle();
    const [{emit, resolve}] = started;
    expect(last()).toMatchObject({status: 'running', completedJobs: 0, totalJobs: 2, step: null});

    emit({type: 'result', result: metrics({rowId: 0})});
    expect(last()).toMatchObject({status: 'running', completedJobs: 1, step: null});

    for (const index of [1, 2]) {
      emit({type: 'result', result: metrics({rowId: 1, candidate: {index, total: 3}, ratio: 5 - index})});
    }
    // Not done until its last candidate is in.
    expect(last()).toMatchObject({status: 'running', completedJobs: 1});
    emit({type: 'result', result: metrics({rowId: 1, candidate: {index: 3, total: 3}, ratio: 2})});
    expect(last()).toMatchObject({status: 'running', completedJobs: 2});
    resolve();
    await settle();

    const state = last();
    expect(state.status).toBe('completed');
    if (state.status !== 'completed') {
      return;
    }
    expect(state.failures).toEqual([]);
    expect(state.results.map((result) => [result.job.rowId, result.job.level, result.candidate])).toEqual([
      [2, 5, null],
      [1, 6, {index: 1, total: 3}],
      [1, 6, {index: 2, total: 3}],
      [1, 6, {index: 3, total: 3}],
    ]);
    // The measurement itself crosses unchanged, without the wire's row id.
    expect(state.results[0]).toMatchObject({srcSize: 1000, compressedSize: 250, ratio: 4, compressMBps: 100});
    expect(state.results[0]).not.toHaveProperty('rowId');
  });

  it('records a row the backend could not measure as a failure, and runs on', async () => {
    const {bridge, started} = fakeBridge();
    const {last} = run(configWith([OPENZL, {rowId: 2, compressor: 'gzip', levels: [5]}]), bridge);
    await settle();
    const [{emit, resolve}] = started;

    emit({type: 'failure', failure: {rowId: 0, candidate: null, message: "Profile not found: 'serial'"}});
    expect(last()).toMatchObject({status: 'running', completedJobs: 1});
    emit({type: 'result', result: metrics({rowId: 1})});
    resolve();
    await settle();

    const state = last();
    expect(state.status).toBe('completed');
    if (state.status !== 'completed') {
      return;
    }
    expect(state.failures.map((failure) => [failure.job.rowId, failure.candidate, failure.message])).toEqual([
      [1, null, "Profile not found: 'serial'"],
    ]);
    expect(state.results.map((result) => result.job.rowId)).toEqual([2]);
  });

  it('has no fraction to give while training, whose length nobody knows', async () => {
    const {bridge, started} = fakeBridge();
    const {last, states} = run(configWith([{...OPENZL, training: {paretoFrontier: true, candidates: 10}}]), bridge);
    await settle();
    expect(last()).toMatchObject({status: 'running', completedJobs: 0, step: null});

    // Nothing moves on its own while it trains: there is no limit to count up to.
    const count = states.length;
    await vi.advanceTimersByTimeAsync(10 * 60_000);
    expect(states).toHaveLength(count);

    started[0].resolve();
    await settle();
  });

  it('moves the bar by the steps the backend reports', async () => {
    const {bridge, started} = fakeBridge();
    const {last} = run(configWith([{...OPENZL, training: {paretoFrontier: true, candidates: 10}}]), bridge);
    await settle();
    const [{emit, resolve}] = started;

    emit({type: 'step', fraction: 0});
    expect(last()).toMatchObject({step: 0});
    emit({type: 'step', fraction: 0.5});
    expect(last()).toMatchObject({step: 0.5});

    resolve();
    await settle();
  });

  it('ends in an error that keeps what was already measured', async () => {
    const {bridge, started} = fakeBridge();
    const {last} = run(configWith([{rowId: 2, compressor: 'gzip', levels: [5]}, OPENZL]), bridge);
    await settle();
    const [{emit, reject}] = started;

    emit({type: 'result', result: metrics({rowId: 0})});
    reject(new Error('Native benchmark process exited unexpectedly (code 1)'));
    await settle();

    const state = last();
    expect(state).toMatchObject({status: 'error', message: 'Native benchmark process exited unexpectedly (code 1)'});
    expect(state.status === 'error' && state.results).toHaveLength(1);
  });

  it('ends in an error, before starting the backend, when the file has no path', async () => {
    const {bridge, started} = fakeBridge('');
    const {last} = run(configWith([OPENZL], new File(['abc'], 'made-up.bin')), bridge);
    await settle();

    expect(last()).toMatchObject({status: 'error', message: 'made-up.bin is not a file on disk'});
    expect(started).toHaveLength(0);
  });

  it('ends in an error, and stays usable, when the bridge cannot name the file', async () => {
    // Electron's `getPathForFile` throws for anything that is not a real File.
    const {bridge, started} = fakeBridge();
    const broken: OpenZLDesktopBridge = {
      ...bridge,
      getPathForFile: () => {
        throw new Error('Not a File');
      },
    };
    const {last} = run(configWith([OPENZL]), broken);
    await settle();
    expect(last()).toMatchObject({status: 'error', message: 'Not a File'});

    run(configWith([OPENZL]), bridge);
    await settle();
    expect(started).toHaveLength(1);
    started[0].resolve();
    await settle();
  });

  it('asks the backend for the bundled sample', async () => {
    const {bridge, started} = fakeBridge('');
    run(configWith([OPENZL], BUNDLED_SAMPLE), bridge);
    await settle();

    expect(started[0].request.input).toEqual({sample: true});
    started[0].resolve();
    await settle();
  });

  it('ends in an error, and stays usable, when the config cannot be planned', async () => {
    // `buildJobs` throws for a config that is not shaped as expected, and a
    // throw out of the executor would leave the page on `loading` for good.
    const {bridge, started} = fakeBridge();
    const malformed = configWith([null as unknown as CompressorConfig]);
    const {last} = run(malformed, bridge);
    await settle();
    expect(last().status).toBe('error');
    expect(started).toHaveLength(0);

    run(configWith([OPENZL]), bridge);
    await settle();
    expect(started).toHaveLength(1);
    started[0].resolve();
    await settle();
  });

  it('ends the run, and cancels the backend, when it answers for a row that was never sent', async () => {
    const {bridge, started} = fakeBridge();
    const {last, states} = run(configWith([OPENZL]), bridge);
    await settle();
    const [{emit, reject, cancel}] = started;

    emit({type: 'result', result: metrics({rowId: 99})});

    expect(last()).toMatchObject({status: 'error', message: expect.stringContaining('did not ask for')});
    expect(cancel).toHaveBeenCalledTimes(1);

    // The cancellation rejecting, and anything the backend still says, must not
    // replace the error that ended the run.
    const count = states.length;
    reject(new Error('Benchmark cancelled'));
    emit({type: 'result', result: metrics({rowId: 0})});
    await settle();
    expect(states).toHaveLength(count);
    expect(last()).toMatchObject({message: expect.stringContaining('did not ask for')});
  });

  it('starts no second run while one is in flight, and frees the next once it ends', async () => {
    // Two `zli` runs at once would skew each other's timings.
    const {bridge, started} = fakeBridge();
    run(configWith([OPENZL]), bridge);
    run(configWith([OPENZL]), bridge);
    await settle();
    expect(started).toHaveLength(1);

    started[0].resolve();
    await settle();
    run(configWith([OPENZL]), bridge);
    await settle();
    expect(started).toHaveLength(2);
    started[1].resolve();
    await settle();
  });
});
