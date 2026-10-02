// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {JobFailure, JobResult, RunConfig, RunState, WorkerMessage, WorkerRequest} from './benchmarkTypes.ts';

/**
 * Drives a run on a worker and folds what comes back into `RunState`.
 *
 * The worker outlives a run so the WASM module is loaded once rather than per
 * press of Run. `cancelRun` is the exception: a synchronous train() cannot be
 * interrupted from JS, so stopping one means discarding the thread it is on,
 * and the next run pays for the module again.
 */
let worker: Worker | undefined;

function getWorker(): Worker {
  worker ??= new Worker(new URL('./benchmarkWorker.ts', import.meta.url), {type: 'module'});
  return worker;
}

/**
 * The run in flight, as one value rather than a worker here and a handful of
 * closures there: whoever ends a run -- itself, an error, or a cancel button
 * one day -- needs the same four things to end it cleanly.
 */
interface ActiveRun {
  readonly worker: Worker;
  readonly onMessage: (event: MessageEvent<WorkerMessage>) => void;
  readonly onError: (event: ErrorEvent) => void;
  readonly onState: (state: RunState) => void;
}

let activeRun: ActiveRun | undefined;

function detach(run: ActiveRun): void {
  run.worker.removeEventListener('message', run.onMessage);
  run.worker.removeEventListener('error', run.onError);
  // Only if it is still the one in flight: a run that ends after another has
  // started owns its own handlers, not the pointer.
  if (activeRun === run) {
    activeRun = undefined;
  }
}

/** Discards the worker, which is the only way to stop a run in progress. */
export function cancelRun(): void {
  worker?.terminate();
  worker = undefined;
}

export function runBenchmark(config: RunConfig, onState: (state: RunState) => void): void {
  // Before anything is posted, not when the worker gets round to saying
  // `loading`. The run button disables off this state, and messages carry no
  // run id -- a second press in that gap would put two runs on one worker and
  // both listeners would read both runs' messages.
  onState({status: 'loading'});

  const results: JobResult[] = [];
  const failures: JobFailure[] = [];
  let totalJobs = 0;
  let step: number | null = null;

  const runWorker = getWorker();
  // `onMessage` and `onError` are function declarations below, so they are
  // already bound here.
  const thisRun: ActiveRun = {worker: runWorker, onMessage, onError, onState};
  const finish = (state: RunState) => {
    detach(thisRun);
    onState(state);
  };
  /**
   * Jobs, not measurements: a trained job arrives as one result per candidate,
   * and it is done when the last of them lands -- `candidate.total` says how
   * many that is. A job that failed is done too, and one that produced some
   * measurements before failing is still the same single job, so both sides
   * count into one set rather than being added together.
   */
  const completedJobs = (): number => {
    const measured = new Map<string, {seen: number; total: number}>();
    for (const result of results) {
      const entry = measured.get(result.job.id);
      if (entry === undefined) {
        measured.set(result.job.id, {seen: 1, total: result.candidate?.total ?? 1});
      } else {
        entry.seen += 1;
      }
    }
    const done = new Set(failures.map((failure) => failure.job.id));
    for (const [id, {seen, total}] of measured) {
      if (seen >= total) {
        done.add(id);
      }
    }
    return Math.min(done.size, totalJobs);
  };

  const progress = (): RunState => ({
    status: 'running',
    completedJobs: completedJobs(),
    totalJobs,
    results: [...results],
    failures: [...failures],
    step,
  });

  function onMessage(event: MessageEvent<WorkerMessage>) {
    const message = event.data;
    switch (message.type) {
      case 'loading':
        onState({status: 'loading'});
        return;
      case 'started':
        totalJobs = message.totalJobs;
        for (const rejected of message.rejected) {
          // `RunState` has nowhere to put a row that never became a job, so
          // for now this only reaches the console.
          console.warn(`OpenZL: row ${String(rejected.rowId)} produced no jobs -- ${rejected.message}`);
        }
        onState(progress());
        return;
      case 'step':
        step = message.fraction;
        onState(progress());
        return;
      case 'result':
        // A job that reported its way through is done reporting.
        step = null;
        results.push(message.result);
        onState(progress());
        return;
      case 'failure':
        // Done reporting too, and it is already counted. Left standing, a
        // training job that died at 90% would lend that 90% to the next
        // job's share of the bar.
        step = null;
        failures.push(message.failure);
        onState(progress());
        return;
      case 'finished':
        finish({status: 'completed', results, failures});
        return;
      case 'failed':
        finish({status: 'error', message: message.message, results, failures});
        return;
    }
  }

  function onError(event: ErrorEvent) {
    // The worker died rather than reporting: the module failed to start, or
    // something threw where nothing was catching. Discard it -- a dead worker
    // never sends another message, so reusing it would leave the next run
    // waiting forever instead of failing.
    cancelRun();
    finish({status: 'error', message: event.message || 'the benchmark worker stopped', results, failures});
  }

  activeRun = thisRun;
  runWorker.addEventListener('message', onMessage);
  runWorker.addEventListener('error', onError);
  const request: WorkerRequest = {config};
  runWorker.postMessage(request);
}
