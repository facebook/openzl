// Copyright (c) Meta Platforms, Inc. and affiliates.

export const COMPRESSOR_NAMES = ['OpenZL', 'zstd', 'gzip'] as const;
export type CompressorName = (typeof COMPRESSOR_NAMES)[number];

/** Profiles shipped by the OpenZL CLI, in the order its `--help` lists them. */
export const OPENZL_PROFILES = [
  'csv',
  'i8',
  'le-i16',
  'le-i32',
  'le-i64',
  'le-u16',
  'le-u32',
  'le-u64',
  'lz',
  'parquet',
  'pytorch',
  'serial',
  'u8',
  'zstd',
] as const;
export type OpenZlProfile = (typeof OPENZL_PROFILES)[number];

/** One-line help for each profile, shown in the row's info popover. */
export const OPENZL_PROFILE_DESCRIPTIONS: Record<OpenZlProfile, string> = {
  csv: 'CSV. Pass optional non-comma separator with --profile-arg <char>.',
  i8: 'Signed 8-bit data',
  'le-i16': 'Little-endian signed 16-bit data',
  'le-i32': 'Little-endian signed 32-bit data',
  'le-i64': 'Little-endian signed 64-bit data',
  'le-u16': 'Little-endian unsigned 16-bit data',
  'le-u32': 'Little-endian unsigned 32-bit data',
  'le-u64': 'Little-endian unsigned 64-bit data',
  lz: 'Trainable LZ compressor',
  parquet: 'Parquet in the canonical format (no compression, plain encoding)',
  pytorch: 'Pytorch model generated from torch.save(). Training is not supported.',
  serial: 'Serial data (aka raw bytes)',
  u8: 'Unsigned 8-bit data',
  zstd: 'Use this profile to train a Zstd dict',
};

function range(min: number, max: number): readonly number[] {
  return Array.from({length: max - min + 1}, (_, index) => min + index);
}

export const OPENZL_LEVELS = range(1, 9);
export const ZSTD_LEVELS = range(1, 19);
export const GZIP_LEVELS = range(1, 9);
export const TRAINED_CANDIDATE_COUNTS = range(1, 5);

export const OPENZL_DEFAULT_LEVEL = 6;
export const ZSTD_DEFAULT_LEVEL = 5;
export const GZIP_DEFAULT_LEVEL = 5;
export const DEFAULT_OPENZL_PROFILE: OpenZlProfile = 'serial';
export const DEFAULT_TRAINED_CANDIDATES = 5;

export function levelsFor(compressor: CompressorName): readonly number[] {
  switch (compressor) {
    case 'OpenZL':
      return OPENZL_LEVELS;
    case 'zstd':
      return ZSTD_LEVELS;
    case 'gzip':
      return GZIP_LEVELS;
  }
}

export function defaultLevelFor(compressor: CompressorName): number {
  switch (compressor) {
    case 'OpenZL':
      return OPENZL_DEFAULT_LEVEL;
    case 'zstd':
      return ZSTD_DEFAULT_LEVEL;
    case 'gzip':
      return GZIP_DEFAULT_LEVEL;
  }
}

export interface CompressorRow {
  id: number;
  compressor: CompressorName;
  profile: OpenZlProfile;
  level: number;
  train: boolean;
  candidates: number;
  optionsOpen: boolean;
}

export function createCompressorRow(id: number, compressor: CompressorName): CompressorRow {
  return {
    id,
    compressor,
    profile: DEFAULT_OPENZL_PROFILE,
    level: defaultLevelFor(compressor),
    // Only OpenZL trains. The flag is invisible on other rows, so leaving it
    // set would hand the run seam a gzip row asking to be trained.
    train: compressor === 'OpenZL',
    candidates: DEFAULT_TRAINED_CANDIDATES,
    optionsOpen: true,
  };
}

export const ITERATIONS_MIN = 1;
export const ITERATIONS_MAX = 15;
export const ITERATIONS_DEFAULT = 5;
export const ITERATION_TICKS = [1, 5, 10, 15] as const;

/**
 * Half the iterations thumb width in pixels. With the slider's default
 * "contain" alignment the thumb center travels between this inset and
 * (width - inset), so anything aligning to the dot — tick labels, in
 * particular — must use the same inset. Keep in sync with the thumb's
 * `boxSize`, which derives from this.
 */
export const ITERATIONS_THUMB_HALF_PX = 8;

/**
 * CSS `left` for a tick label so its center sits exactly under the dot when
 * the slider holds that value: the proportional position within the thumb's
 * inset travel, paired with `translateX(-50%)` at the call site.
 */
export function tickLabelLeft(tick: number): string {
  const fraction = Math.round(((tick - ITERATIONS_MIN) / (ITERATIONS_MAX - ITERATIONS_MIN)) * 10000) / 10000;
  return `calc(${ITERATIONS_THUMB_HALF_PX}px + ${fraction} * (100% - ${ITERATIONS_THUMB_HALF_PX * 2}px))`;
}
