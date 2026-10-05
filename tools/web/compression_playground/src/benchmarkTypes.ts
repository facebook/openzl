// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {BenchmarkResult as BenchmarkMetrics, ProfileValue} from '../../../wasm/js/wasm_api.js';
import {isTrainableProfile, type CompressorRow, type OpenZlProfile} from './compressors.ts';
import {WASM_PROFILE} from './wasmProfiles.ts';

interface CompressorConfigBase {
  readonly rowId: number;
  /**
   * One measurement per level. A row selects a single level today, but the
   * design sweeps several per compressor so each contributes a curve rather
   * than a point, so the plural shape is what crosses the worker boundary.
   */
  readonly levels: readonly number[];
}

/**
 * Present only when the row asks for training, so an untrained run cannot carry
 * a candidate count the worker would then act on. Training also takes a time
 * budget, which belongs here once the UI exposes it.
 */
export interface TrainingConfig {
  /**
   * Stated rather than assumed: the binding ignores `maxNumCandidates` unless
   * `paretoFrontier` is set, and without it training returns a single
   * best-ratio compressor. A candidate count only means anything alongside the
   * frontier, so the two travel together.
   */
  readonly paretoFrontier: true;
  /**
   * How many of the trained compressors to keep, not how many are built --
   * `wasm_api.d.ts` is explicit that this bounds the results, not the work.
   * The binding raises anything under `OPENZL_WASM_TRAIN_PARETO_CANDIDATES`
   * (6) and throws over 25, the window `TRAINED_CANDIDATE_COUNTS` offers.
   */
  readonly candidates: number;
}

export interface OpenZlCompressorConfig extends CompressorConfigBase {
  readonly compressor: 'OpenZL';
  readonly profile: OpenZlProfile;
  readonly training: TrainingConfig | null;
}

export interface ZstdCompressorConfig extends CompressorConfigBase {
  readonly compressor: 'zstd';
}

export interface GzipCompressorConfig extends CompressorConfigBase {
  readonly compressor: 'gzip';
}

export type CompressorConfig = OpenZlCompressorConfig | ZstdCompressorConfig | GzipCompressorConfig;

export interface RunConfig {
  readonly input: File;
  readonly compressors: readonly CompressorConfig[];
  readonly iterations: number;
}

interface BenchmarkJobBase {
  readonly id: string;
  readonly rowId: number;
  readonly level: number;
  readonly iterations: number;
}

export interface OpenZlBenchmarkJob extends BenchmarkJobBase {
  readonly compressor: 'OpenZL';
  readonly profile: ProfileValue;
  readonly training: TrainingConfig | null;
}

export interface ZstdBenchmarkJob extends BenchmarkJobBase {
  readonly compressor: 'zstd';
}

export interface GzipBenchmarkJob extends BenchmarkJobBase {
  readonly compressor: 'gzip';
}

export type BenchmarkJob = OpenZlBenchmarkJob | ZstdBenchmarkJob | GzipBenchmarkJob;

export interface JobResult extends BenchmarkMetrics {
  readonly job: BenchmarkJob;
}

export interface JobFailure {
  readonly job: BenchmarkJob;
  readonly message: string;
}

interface RunOutcome {
  readonly results: readonly JobResult[];
  readonly failures: readonly JobFailure[];
}

export interface IdleRunState {
  readonly status: 'idle';
}

export interface LoadingRunState {
  readonly status: 'loading';
}

export interface RunningRunState extends RunOutcome {
  readonly status: 'running';
  readonly completedJobs: number;
  readonly totalJobs: number;
}

export interface CompletedRunState extends RunOutcome {
  readonly status: 'completed';
}

export interface ErrorRunState extends RunOutcome {
  readonly status: 'error';
  readonly message: string;
}

export type RunState = IdleRunState | LoadingRunState | RunningRunState | CompletedRunState | ErrorRunState;

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}

export function toCompressorConfig(row: CompressorRow): CompressorConfig {
  switch (row.compressor) {
    case 'OpenZL':
      return {
        rowId: row.id,
        compressor: row.compressor,
        levels: [row.level],
        profile: row.profile,
        // The request is kept as the user left it and honoured here, so
        // passing through an untrainable profile cannot cost the row a setting
        // it would never get back.
        training:
          row.trainRequested && isTrainableProfile(row.profile)
            ? {paretoFrontier: true, candidates: row.candidates}
            : null,
      };
    case 'zstd':
    case 'gzip':
      return {rowId: row.id, compressor: row.compressor, levels: [row.level]};
    default:
      return assertNever(row);
  }
}

/**
 * A compressor the run cannot measure, reported rather than skipped: a row that
 * contributes nothing and says nothing reads as a benchmark that lost it.
 */
export interface RejectedCompressor {
  readonly rowId: number;
  readonly message: string;
}

export interface RunPlan {
  readonly jobs: readonly BenchmarkJob[];
  readonly rejected: readonly RejectedCompressor[];
}

/**
 * Flattens the run into one job per compressor per level, which is the unit the
 * worker measures and the unit `totalJobs` counts.
 */
export function buildJobs(config: RunConfig): RunPlan {
  const jobs: BenchmarkJob[] = [];
  const rejected: RejectedCompressor[] = [];

  for (const compressor of config.compressors) {
    const base = (level: number) => ({
      id: `${compressor.rowId}-${compressor.compressor}-${level}`,
      rowId: compressor.rowId,
      level,
      iterations: config.iterations,
    });

    switch (compressor.compressor) {
      case 'OpenZL': {
        const profile = WASM_PROFILE[compressor.profile];
        if (profile === null) {
          // The picker refuses these profiles, so this is a backstop for a
          // config assembled anywhere else.
          rejected.push({
            rowId: compressor.rowId,
            message: `The ${compressor.profile} profile has no browser build`,
          });
          break;
        }
        for (const level of compressor.levels) {
          jobs.push({...base(level), compressor: 'OpenZL', profile, training: compressor.training});
        }
        break;
      }
      case 'zstd':
        for (const level of compressor.levels) {
          jobs.push({...base(level), compressor: 'zstd'});
        }
        break;
      case 'gzip':
        for (const level of compressor.levels) {
          jobs.push({...base(level), compressor: 'gzip'});
        }
        break;
      default:
        assertNever(compressor);
    }
  }

  return {jobs, rejected};
}

export function isRunInProgress(runState: RunState): boolean {
  switch (runState.status) {
    case 'loading':
    case 'running':
      return true;
    case 'idle':
    case 'completed':
    case 'error':
      return false;
    default:
      return assertNever(runState);
  }
}
