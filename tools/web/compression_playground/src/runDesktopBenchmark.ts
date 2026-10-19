// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {BenchmarkEvent, BenchmarkRow} from '../../../desktop/compression_playground/electron/native-worker.cjs';
import {
  buildJobs,
  type BenchmarkJob,
  type RejectedCompressor,
  type RunConfig,
  type RunState,
  type WorkerMessage,
} from './benchmarkTypes.ts';
import {inputForFile, type BenchmarkRun, type OpenZLDesktopBridge} from './desktop.ts';
import {trackRun} from './runTracker.ts';

/**
 * Runs a benchmark through the desktop app's `zli`. The backend reports in the
 * browser worker's message vocabulary, so once each row is matched back to its
 * job the same `RunTracker` folds it into `RunState`, and everything past the
 * Run button -- progress, table, charts -- is one code path.
 */

let inFlight = false;

/**
 * The wire row for one job. The backend takes one level per row, so a job --
 * one compressor at one level -- is exactly one row, and its position is the
 * id the results come back under.
 */
function rowFor(job: BenchmarkJob, id: number): BenchmarkRow {
  if (job.compressor === 'OpenZL') {
    return {
      id,
      compressor: 'OpenZL',
      level: job.level,
      profile: job.profile,
      trainRequested: job.training !== null,
      ...(job.training === null ? {} : {candidates: job.training.candidates}),
      profileArg: job.profileArg,
    };
  }
  return {id, compressor: job.compressor, level: job.level};
}

/** The browser's message for a backend event, or undefined for a row this run never sent. */
function toWorkerMessage(event: BenchmarkEvent, jobs: readonly BenchmarkJob[]): WorkerMessage | undefined {
  switch (event.type) {
    case 'step':
      return event;
    case 'result': {
      const {rowId, ...measurement} = event.result;
      const job = jobs[rowId];
      return job === undefined ? undefined : {type: 'result', result: {...measurement, job}};
    }
    case 'failure': {
      const {rowId, ...failure} = event.failure;
      const job = jobs[rowId];
      return job === undefined ? undefined : {type: 'failure', failure: {...failure, job}};
    }
  }
}

export function runDesktopBenchmark(
  config: RunConfig,
  bridge: OpenZLDesktopBridge,
  onState: (state: RunState) => void,
): void {
  // The Run button disables off the state below, which normally keeps a second
  // run from getting here; this keeps any caller that does from starting two
  // `zli` runs that would skew each other's timings.
  if (inFlight) {
    return;
  }
  inFlight = true;

  // Before anything else: the button has to disable now, not when the backend
  // gets round to starting.
  onState({status: 'loading'});

  const tracker = trackRun();
  let jobs: readonly BenchmarkJob[] = [];
  let rejected: readonly RejectedCompressor[] = [];
  let run: BenchmarkRun | undefined;
  let finished = false;

  // Once only: a run ended by an unknown row is then cancelled, and the
  // cancellation rejecting must not replace the error that caused it.
  const finish = (state: RunState) => {
    if (finished) {
      return;
    }
    finished = true;
    inFlight = false;
    onState(state);
  };
  const fail = (message: string) => {
    finish(tracker.error(message));
  };

  function onEvent(event: BenchmarkEvent) {
    if (finished) {
      return;
    }
    const message = toWorkerMessage(event, jobs);
    if (message === undefined) {
      // The backend answered for a row this run never sent. Nothing sensible
      // can be done with its numbers, and it is still running.
      run?.cancel();
      fail('The desktop backend reported a result for a row this run did not ask for');
      return;
    }
    onState(tracker.apply(message));
  }

  // Everything that can throw is inside the chain rather than called here: a
  // throw out of this function would leave the page on `loading` with every
  // later run turned away. `getPathForFile` throws for anything it cannot name
  // a path for, and `buildJobs` for a config that is not shaped as expected.
  Promise.resolve()
    .then(() => {
      ({jobs, rejected} = buildJobs(config, {desktop: true}));
      return inputForFile(config.input, bridge);
    })
    .then((input) => {
      run = bridge.startBenchmark(
        {input, iterations: config.iterations, compressors: jobs.map((job, id) => rowFor(job, id))},
        onEvent,
      );
      onState(tracker.apply({type: 'started', totalJobs: jobs.length, rejected}));
      return run.done;
    })
    .then(
      () => {
        finish(tracker.apply({type: 'finished'}));
      },
      (error: unknown) => {
        fail(error instanceof Error ? error.message : String(error));
      },
    );
}
