// Copyright (c) Meta Platforms, Inc. and affiliates.

import {createOpenZL, type OpenZL} from '../../../wasm/js/wasm_api.js';
import {benchmarkGzip, benchmarkOpenZL, benchmarkZstd} from './benchmark.ts';
import {buildJobs, type BenchmarkJob, type JobResult, type RunConfig, type WorkerMessage} from './benchmarkTypes.ts';

/** Where a message goes. The worker posts; a test collects. */
type Emit = (message: WorkerMessage) => void;

/**
 * One module for the worker's life. Instantiating costs several megabytes and
 * about 70ms, and nothing about it is per-run.
 */
let modulePromise: Promise<OpenZL> | undefined;

function loadOpenZL(): Promise<OpenZL> {
  modulePromise ??= createOpenZL().catch((error: unknown) => {
    modulePromise = undefined;
    throw error;
  });
  return modulePromise;
}

/**
 * Every measurement a job produces. One for anything but training, which
 * returns a compressor per point on its Pareto frontier.
 *
 * Yielded rather than collected: a trained job runs for minutes, so holding its
 * measurements back would leave the page still until the last candidate is
 * benchmarked, and anything thrown part way would take the candidates already
 * measured with it.
 */
async function* measure(job: BenchmarkJob, data: Uint8Array, openzl: OpenZL | null): AsyncGenerator<JobResult> {
  switch (job.compressor) {
    case 'OpenZL': {
      if (openzl === null) {
        throw new Error('the OpenZL module is not loaded');
      }
      const base = openzl.getSerializedCompressor(job.profile, job.level);
      if (job.training === null) {
        yield {...(await benchmarkOpenZL(openzl, data, base, job.iterations)), job, candidate: null};
        return;
      }
      const frontier = openzl.train(data, base, {
        paretoFrontier: job.training.paretoFrontier,
        maxNumCandidates: job.training.candidates,
        ...(job.training.maxTimeSecs === undefined ? {} : {maxTimeSecs: job.training.maxTimeSecs}),
      });
      for (const [index, compressor] of frontier.entries()) {
        yield {
          ...(await benchmarkOpenZL(openzl, data, compressor, job.iterations)),
          job,
          // The binding returns the frontier best ratio first, which is the
          // order the table ranks and labels by.
          candidate: {index: index + 1, total: frontier.length},
        };
      }
      return;
    }
    case 'zstd':
      yield {...(await benchmarkZstd(data, job.level, job.iterations)), job, candidate: null};
      return;
    case 'gzip':
      yield {...(await benchmarkGzip(data, job.level, job.iterations)), job, candidate: null};
      return;
  }
}

export async function measureRun(config: RunConfig, post: Emit): Promise<void> {
  post({type: 'loading'});
  const data = new Uint8Array(await config.input.arrayBuffer());
  const {jobs, rejected} = buildJobs(config);
  post({type: 'started', totalJobs: jobs.length, rejected});

  // Awaited here so the cost is paid once, but a failure is carried into the
  // loop rather than thrown out of the run: zstd and gzip need nothing from
  // this module, and the whole point of the per-job catch below is that one
  // compressor going down does not take the others with it.
  let openzl: OpenZL | null = null;
  let openzlError: unknown;
  if (jobs.some((job) => job.compressor === 'OpenZL')) {
    try {
      openzl = await loadOpenZL();
    } catch (error) {
      openzlError = error;
    }
  }

  for (const job of jobs) {
    try {
      if (job.compressor === 'OpenZL' && openzlError !== undefined) {
        throw openzlError;
      }
      for await (const result of measure(job, data, openzl)) {
        post({type: 'result', result});
      }
    } catch (error) {
      // Reported and stepped over: one compressor that cannot be measured
      // should not cost the others their results.
      post({type: 'failure', failure: {job, message: error instanceof Error ? error.message : String(error)}});
    }
  }

  post({type: 'finished'});
}
