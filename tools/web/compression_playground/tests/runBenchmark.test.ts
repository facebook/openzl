// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import {afterEach, describe, expect, it, vi} from 'vitest';
import type {RunConfig, RunState} from '../src/benchmarkTypes.ts';

/**
 * A stand-in for the real worker: jsdom has no `Worker`, and these cases are
 * about what `runBenchmark` does around it rather than what it computes.
 */
class FakeWorker {
  static instances: FakeWorker[] = [];
  readonly listeners = new Map<string, ((event: unknown) => void)[]>();
  readonly posted: unknown[] = [];
  terminated = false;

  constructor() {
    FakeWorker.instances.push(this);
  }
  addEventListener(type: string, handler: (event: unknown) => void) {
    this.listeners.set(type, [...(this.listeners.get(type) ?? []), handler]);
  }
  removeEventListener(type: string, handler: (event: unknown) => void) {
    this.listeners.set(
      type,
      (this.listeners.get(type) ?? []).filter((h) => h !== handler),
    );
  }
  postMessage(message: unknown) {
    this.posted.push(message);
  }
  terminate() {
    this.terminated = true;
  }
  emit(type: string, event: unknown) {
    for (const handler of [...(this.listeners.get(type) ?? [])]) {
      handler(event);
    }
  }
}

const config = {input: new File(['x'], 'x.bin'), compressors: [], iterations: 1} as unknown as RunConfig;

async function freshModule() {
  vi.resetModules();
  FakeWorker.instances = [];
  vi.stubGlobal('Worker', FakeWorker);
  return import('../src/runBenchmark.ts');
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('runBenchmark', () => {
  it('reports itself busy before the worker has said anything', async () => {
    // The run button disables off this state. Messages carry no run id, so a
    // second press in the gap would put two runs on one worker and each
    // listener would read both runs' messages.
    const {runBenchmark} = await freshModule();
    const states: RunState[] = [];

    runBenchmark(config, (state) => states.push(state));

    expect(states).toHaveLength(1);
    expect(states[0].status).toBe('loading');
  });

  it('discards a worker that died rather than handing it to the next run', async () => {
    const {runBenchmark} = await freshModule();
    const states: RunState[] = [];

    runBenchmark(config, (state) => states.push(state));
    const [first] = FakeWorker.instances;
    first.emit('error', {message: 'worker exploded'});

    expect(states.at(-1)).toMatchObject({status: 'error', message: 'worker exploded'});
    expect(first.terminated).toBe(true);

    // A dead worker never sends another message, so reusing it would leave the
    // next run waiting rather than failing.
    runBenchmark(config, (state) => states.push(state));
    expect(FakeWorker.instances).toHaveLength(2);
  });

  it('counts a trained job once, and only once its last candidate lands', async () => {
    const {runBenchmark} = await freshModule();
    const states: RunState[] = [];
    const candidate = (index: number) => ({
      type: 'result',
      result: {job: {id: 'openzl-6'}, candidate: {index, total: 3}},
    });

    runBenchmark(config, (state) => states.push(state));
    const [worker] = FakeWorker.instances;
    worker.emit('message', {data: {type: 'started', totalJobs: 2, rejected: []}});

    worker.emit('message', {data: candidate(1)});
    worker.emit('message', {data: candidate(2)});
    // Two of three: the job is still running, and saying otherwise would run
    // the bar ahead of the work.
    expect(states.at(-1)).toMatchObject({completedJobs: 0, totalJobs: 2});

    worker.emit('message', {data: candidate(3)});
    expect(states.at(-1)).toMatchObject({completedJobs: 1});

    // Measured some, then failed: still the one job.
    worker.emit('message', {data: {type: 'failure', failure: {job: {id: 'openzl-6'}, message: 'stopped'}}});
    expect(states.at(-1)).toMatchObject({completedJobs: 1});
  });

  it("drops a job's step once it stops reporting, however it stopped", async () => {
    const {runBenchmark} = await freshModule();
    const states: RunState[] = [];

    runBenchmark(config, (state) => states.push(state));
    const [worker] = FakeWorker.instances;
    worker.emit('message', {data: {type: 'started', totalJobs: 2, rejected: []}});

    worker.emit('message', {data: {type: 'step', fraction: 0.9}});
    expect(states.at(-1)).toMatchObject({step: 0.9});

    worker.emit('message', {data: {type: 'result', result: {job: {id: 'openzl-6'}, candidate: null}}});
    expect(states.at(-1)).toMatchObject({step: null});

    // A job that died partway through reporting has stopped too. Left
    // standing, its 90% would lend itself to the next job's share of the bar.
    worker.emit('message', {data: {type: 'step', fraction: 0.9}});
    worker.emit('message', {data: {type: 'failure', failure: {job: {id: 'zstd-1'}, message: 'stopped'}}});
    expect(states.at(-1)).toMatchObject({step: null});
  });

  it('keeps one worker across runs that end normally', async () => {
    // Instantiating the module costs megabytes, so it outlives a single run.
    const {runBenchmark} = await freshModule();
    const states: RunState[] = [];

    runBenchmark(config, (state) => states.push(state));
    FakeWorker.instances[0].emit('message', {data: {type: 'finished'}});
    runBenchmark(config, (state) => states.push(state));

    expect(FakeWorker.instances).toHaveLength(1);
    expect(FakeWorker.instances[0].posted).toHaveLength(2);
  });
});
