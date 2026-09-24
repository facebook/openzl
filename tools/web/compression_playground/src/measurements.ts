// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {CompressorName} from './compressors.ts';
import type {JobResult, RunState} from './benchmarkTypes.ts';
import {WASM_PROFILE} from './wasmProfiles.ts';

/** Columns the table can be ordered by. `codec` is the grouped default. */
export type SortKey = 'codec' | 'detail' | 'compressedSize' | 'ratio' | 'compressMBps' | 'decompressMBps';
export type SortDirection = 'asc' | 'desc';

/** Named ends of a trained frontier, which is ordered best ratio first. */
export type FrontierTag = 'max ratio' | 'balanced' | 'max speed';

export interface MeasurementGroup {
  /** The compressor row these came from; groups are per row, not per codec. */
  readonly rowId: number;
  readonly compressor: CompressorName;
  /** `trained · serial` for a trained row, `6 levels` otherwise. */
  readonly detail: string;
  readonly rows: readonly JobResult[];
}

/**
 * A single measurement needs no rank to read, and a frontier of two has no
 * middle. Three or more take all three names, the middle by rank rather than
 * by any property of the numbers, which matches how the frontier is ordered.
 */
export function frontierTag(index: number, total: number): FrontierTag | null {
  if (total < 2) {
    return null;
  }
  if (index === 1) {
    return 'max ratio';
  }
  if (index === total) {
    return 'max speed';
  }
  return total >= 3 && index === Math.ceil(total / 2) ? 'balanced' : null;
}

function profileName(result: JobResult): string | null {
  if (result.job.compressor !== 'OpenZL') {
    return null;
  }
  const profile = result.job.profile;
  return Object.entries(WASM_PROFILE).find(([, value]) => value === profile)?.[0] ?? null;
}

/**
 * What one measurement ran at, for the `LEVEL / PROFILE` column. A trained
 * candidate has nothing of its own to put here -- the profile is on the group
 * row above it and every candidate shares it -- so the design leaves a dash.
 */
export function rowDetail(result: JobResult): string {
  if (result.candidate !== null) {
    return '-';
  }
  return profileName(result) ?? `Level ${String(result.job.level)}`;
}

function groupDetail(rows: readonly JobResult[]): string {
  const [first] = rows;
  const name = profileName(first);
  if (name !== null) {
    return first.candidate === null ? name : `trained · ${name}`;
  }
  return rows.length === 1 ? '1 level' : `${String(rows.length)} levels`;
}

/**
 * One group per compressor row, in the order the rows were configured, with
 * each group's measurements left in the order they were produced: ascending
 * level for zstd and gzip, descending ratio for a trained frontier.
 */
export function groupMeasurements(results: readonly JobResult[]): readonly MeasurementGroup[] {
  const byRow = new Map<number, JobResult[]>();
  for (const result of results) {
    const rows = byRow.get(result.job.rowId);
    if (rows === undefined) {
      byRow.set(result.job.rowId, [result]);
    } else {
      rows.push(result);
    }
  }
  return [...byRow.entries()].map(([rowId, rows]) => ({
    rowId,
    compressor: rows[0].job.compressor,
    detail: groupDetail(rows),
    rows,
  }));
}

export function sortGroups(
  groups: readonly MeasurementGroup[],
  key: SortKey,
  direction: SortDirection,
): readonly MeasurementGroup[] {
  if (key === 'codec') {
    return groups;
  }
  const sign = direction === 'asc' ? 1 : -1;
  // Rows move within their group rather than across it: the grouping is what
  // makes a codec's several measurements legible as one curve, and sorting is
  // for reading a column, not for dissolving that.
  // A trained candidate has no level of its own -- the column is a dash and
  // every candidate in the group shares the job's -- so its rank stands in,
  // which is the order the frontier is already in.
  const valueOf = (result: JobResult) =>
    key === 'detail' ? (result.candidate?.index ?? result.job.level) : result[key];
  return groups.map((group) => ({
    ...group,
    rows: [...group.rows].sort((a, b) => (valueOf(a) - valueOf(b)) * sign),
  }));
}

export function formatBytes(bytes: number): string {
  if (bytes < 1000) {
    return `${String(bytes)} B`;
  }
  const units = ['KB', 'MB', 'GB'];
  let value = bytes / 1000;
  let unit = 0;
  while (value >= 1000 && unit < units.length - 1) {
    value /= 1000;
    unit += 1;
  }
  return `${value.toFixed(1)} ${units[unit]}`;
}

export const formatRatio = (ratio: number): string => `${ratio.toFixed(2)}×`;
export const formatSpeed = (mbps: number): string => `${String(Math.round(mbps))} MB/s`;

/** The span a group covers in one column, low to high. */
export function formatRange(values: readonly number[], format: (value: number) => string): string {
  const low = Math.min(...values);
  const high = Math.max(...values);
  return low === high ? format(low) : `${format(low)} - ${format(high)}`;
}

/**
 * `2764× - 15235×`: a group's span is for scale, and the design drops the
 * decimals from it. Real zstd and gzip ratios sit under 10, though, where
 * rounding turns 4.51 to 5.38 into `5× - 5×` -- so the decimals come back
 * whenever they are the only thing separating the ends.
 */
export function formatRatioRange(values: readonly number[]): string {
  const low = Math.min(...values);
  const high = Math.max(...values);
  if (low !== high && Math.round(low) === Math.round(high)) {
    return formatRange(values, formatRatio);
  }
  return formatRange(values, (value) => `${String(Math.round(value))}×`);
}

/** `38 - 310 MB/s` rather than `38 MB/s - 310 MB/s`. */
export function formatSpeedRange(values: readonly number[]): string {
  const low = Math.round(Math.min(...values));
  const high = Math.round(Math.max(...values));
  return low === high ? `${String(low)} MB/s` : `${String(low)} - ${String(high)} MB/s`;
}

/**
 * How much of its track a bar fills, against the largest value anywhere in the
 * run rather than within its group. Per-group would give every codec's best a
 * full bar, which reads as a tie between things the tool exists to tell apart.
 */
export function barFraction(value: number, peak: number): number {
  return peak <= 0 ? 0 : Math.max(0.02, Math.min(1, value / peak));
}

export interface Peaks {
  readonly ratio: number;
  readonly compressMBps: number;
  readonly decompressMBps: number;
}

export function peaksOf(results: readonly JobResult[]): Peaks {
  return {
    ratio: Math.max(0, ...results.map((result) => result.ratio)),
    compressMBps: Math.max(0, ...results.map((result) => result.compressMBps)),
    decompressMBps: Math.max(0, ...results.map((result) => result.decompressMBps)),
  };
}

export interface RunSummary {
  /** Absent when nothing was measured, which is the only source for it. */
  readonly srcSize: number | null;
  readonly succeeded: number;
  readonly failed: number;
}

/**
 * What a finished run amounts to, counted in compressor rows rather than jobs.
 * A row is what the reader added, and the design words it that way; jobs would
 * double-count a row that was expanded across six levels.
 *
 * A row counts as failed only when it produced nothing at all. One level of six
 * failing leaves the row succeeded, and the failure list under the table names
 * it. Rows rejected before becoming jobs are in neither count -- nothing
 * carries them this far.
 */
export function runSummary(runState: RunState): RunSummary | null {
  if (runState.status !== 'completed') {
    return null;
  }
  const succeeded = new Set(runState.results.map((result) => result.job.rowId));
  const failed = new Set(
    runState.failures.map((failure) => failure.job.rowId).filter((rowId) => !succeeded.has(rowId)),
  );
  return {
    srcSize: runState.results[0]?.srcSize ?? null,
    succeeded: succeeded.size,
    failed: failed.size,
  };
}

/** `idle` and `loading` carry no outcome yet. */
export function resultsOf(runState: RunState): readonly JobResult[] {
  return runState.status === 'idle' || runState.status === 'loading' ? [] : runState.results;
}
