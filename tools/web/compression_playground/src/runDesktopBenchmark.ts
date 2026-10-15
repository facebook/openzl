// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {
  BenchmarkEvent,
  BenchmarkResult,
  BenchmarkRow,
} from '../../../desktop/compression_playground/electron/native-worker.cjs';
import {buildJobs, type BenchmarkJob, type JobResult, type RunConfig, type RunState} from './benchmarkTypes.ts';
import {inputForFile, type BenchmarkRun, type OpenZLDesktopBridge} from './desktop.ts';

/**
 * Runs a benchmark through the desktop app's `zli` and folds what comes back
 * into the same `RunState` the browser's worker produces, so everything past
 * the Run button -- progress, table, charts -- is one code path.
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

/**
 * A trained job's candidates arrive together, best ratio first and counted from
 * 0 on the wire; the page counts them from 1. Anything else is one measurement.
 */
function toJobResults(job: BenchmarkJob, rows: readonly BenchmarkResult[]): JobResult[] {
  const trained = job.compressor === 'OpenZL' && job.training !== null;
  return rows.map((row) => ({
    iterations: row.iterations,
    srcSize: row.srcSize,
    compressedSize: row.compressedSize,
    ratio: row.ratio,
    compressMs: row.compressMs,
    decompressMs: row.decompressMs,
    compressMBps: row.compressMBps,
    decompressMBps: row.decompressMBps,
    job,
    candidate: trained ? {index: row.candidate + 1, total: rows.length} : null,
  }));
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

  const results: JobResult[] = [];
  let jobs: readonly BenchmarkJob[] = [];
  let run: BenchmarkRun | undefined;
  let completedJobs = 0;
  let step: number | null = null;
  let current = 0;
  let finished = false;

  const progress = (): RunState => ({
    status: 'running',
    completedJobs,
    totalJobs: jobs.length,
    results: [...results],
    failures: [],
    step,
  });
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
    finish({status: 'error', message, results, failures: []});
  };

  function onEvent(event: BenchmarkEvent) {
    if (finished) {
      return;
    }
    if (event.type === 'partial') {
      // The row's job is whichever it says it is, or the one that was running
      // if it measured nothing and so has no result to say.
      const job: BenchmarkJob | undefined = jobs[event.results[0]?.rowId ?? current];
      if (job === undefined) {
        // The backend answered for a row this run never sent. Nothing sensible
        // can be done with its numbers, and it is still running.
        run?.cancel();
        fail('The desktop backend reported a result for a row this run did not ask for');
        return;
      }
      results.push(...toJobResults(job, event.results));
      completedJobs += 1;
      step = null;
      onState(progress());
      return;
    }
    const stage = event.progress;
    current = stage.rowId;
    switch (stage.stage) {
      case 'benchmark':
      case 'train':
        // Training has no known length -- `zli train` runs until it is done --
        // so there is no fraction to report for it, and a quick benchmark has
        // nothing worth one.
        step = null;
        break;
      case 'candidates':
        step = (stage.candidate - 1) / stage.candidateCount;
        break;
    }
    onState(progress());
  }

  // Everything that can throw is inside the chain rather than called here: a
  // throw out of this function would leave the page on `loading` with every
  // later run turned away. `getPathForFile` throws for anything it cannot name
  // a path for, and `buildJobs` for a config that is not shaped as expected.
  Promise.resolve()
    .then(() => {
      const plan = buildJobs(config, {desktop: true});
      jobs = plan.jobs;
      for (const row of plan.rejected) {
        console.warn(`OpenZL: row ${String(row.rowId)} produced no jobs -- ${row.message}`);
      }
      return inputForFile(config.input, bridge);
    })
    .then((input) => {
      run = bridge.startBenchmark(
        {input, iterations: config.iterations, compressors: jobs.map((job, id) => rowFor(job, id))},
        onEvent,
      );
      onState(progress());
      return run.results;
    })
    .then(
      () => {
        finish({status: 'completed', results, failures: []});
      },
      (error: unknown) => {
        fail(error instanceof Error ? error.message : String(error));
      },
    );
}
