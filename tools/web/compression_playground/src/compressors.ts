// Copyright (c) Meta Platforms, Inc. and affiliates.

export const COMPRESSOR_NAMES = ['OpenZL', 'zstd', 'gzip'] as const;
export type CompressorName = (typeof COMPRESSOR_NAMES)[number];

/**
 * Every profile the OpenZL CLI registers, in the order `zli list-profiles`
 * prints them -- `compressProfiles()` is a `std::map`, so that is alphabetical.
 *
 * Hand-copied, and it has already gone stale once: this list was transcribed
 * from a `list-profiles` dump taken before the big-endian profiles landed, and
 * stayed ten short without anything noticing. Deriving it needs
 * `compressProfiles()` reachable from JS, which it is not -- the WASM module
 * only exposes the nine it implements. Until then, adding a profile to
 * `cli/utils/compress_profiles.cpp` means adding it here too.
 */
export const OPENZL_PROFILES = [
  'be-i16',
  'be-i32',
  'be-i64',
  'be-u16',
  'be-u32',
  'be-u64',
  'csv',
  'i8',
  'le-i16',
  'le-i32',
  'le-i64',
  'le-u16',
  'le-u32',
  'le-u64',
  'lz',
  'numeric-ml-selector-64',
  'parquet',
  'pytorch',
  'sao',
  'sddl',
  'sddl2',
  'serial',
  'u8',
  'zstd',
] as const;
export type OpenZlProfile = (typeof OPENZL_PROFILES)[number];

/** Help text for each profile, shown in the profile picker's tooltips. */
export const OPENZL_PROFILE_DESCRIPTIONS: Record<OpenZlProfile, string> = {
  'be-i16': 'Big-endian signed 16-bit data',
  'be-i32': 'Big-endian signed 32-bit data',
  'be-i64': 'Big-endian signed 64-bit data',
  'be-u16': 'Big-endian unsigned 16-bit data',
  'be-u32': 'Big-endian unsigned 32-bit data',
  'be-u64': 'Big-endian unsigned 64-bit data',
  csv: 'CSV. Pass optional non-comma separator with --profile-arg <char>.',
  i8: 'Signed 8-bit data',
  'le-i16': 'Little-endian signed 16-bit data',
  'le-i32': 'Little-endian signed 32-bit data',
  'le-i64': 'Little-endian signed 64-bit data',
  'le-u16': 'Little-endian unsigned 16-bit data',
  'le-u32': 'Little-endian unsigned 32-bit data',
  'le-u64': 'Little-endian unsigned 64-bit data',
  lz: 'Trainable LZ compressor',
  'numeric-ml-selector-64': '64 bit numeric data using ml selectors (Placeholder)',
  parquet: 'Parquet in the canonical format (no compression, plain encoding)',
  pytorch: 'Pytorch model generated from torch.save(). Training is not supported.',
  sao: 'SAO format from the Silesia corpus',
  sddl: 'Data that can be parsed using the Simple Data Description Language. Pass a path to the data description file with --profile-arg.',
  sddl2:
    'Data that can be parsed using Simple Data Description Language v2 (https://openzl.org/sddl/). Pass a path to the description file with --profile-arg.',
  serial: 'Serial data (aka raw bytes)',
  u8: 'Unsigned 8-bit data',
  zstd: 'Use this profile to train a Zstd dict',
};

/**
 * The CLI cannot train a pytorch model -- its own profile description says so --
 * so neither the picker nor a run built from a row may ask for it.
 */
export function isTrainableProfile(profile: OpenZlProfile): boolean {
  return profile !== 'pytorch';
}

function range(min: number, max: number): readonly number[] {
  return Array.from({length: max - min + 1}, (_, index) => min + index);
}

export const OPENZL_LEVELS = range(1, 9);
export const ZSTD_LEVELS = range(1, 19);
export const GZIP_LEVELS = range(1, 9);
/**
 * The window the WASM trainer honours: it silently raises anything below
 * `OPENZL_WASM_TRAIN_PARETO_CANDIDATES` (6, the floor of its pruning) and
 * throws above `MAX_TRAIN_CANDIDATES` (25, which bounds its allocations).
 */
export const TRAINED_CANDIDATE_COUNTS = range(6, 25);

export const OPENZL_DEFAULT_LEVEL = 6;
export const ZSTD_DEFAULT_LEVEL = 5;
export const GZIP_DEFAULT_LEVEL = 5;
export const DEFAULT_OPENZL_PROFILE: OpenZlProfile = 'serial';
export const DEFAULT_TRAINED_CANDIDATES = 6;

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

export interface CompressorRowBase {
  id: number;
  /** Range depends on the compressor; see `levelsFor`. */
  level: number;
}

/**
 * Profile, training and the collapsible options section are OpenZL-only, so
 * they live on this variant rather than on every row. `compressor` is the
 * discriminant: narrowing on it is what stops a gzip row from carrying a
 * training flag the run seam would then act on.
 */
export interface OpenZlRow extends CompressorRowBase {
  compressor: 'OpenZL';
  profile: OpenZlProfile;
  /** What the user asked for, not what will happen: see `isTrainableProfile`. */
  trainRequested: boolean;
  candidates: number;
  optionsOpen: boolean;
}

/** Kept apart from `GzipRow` despite matching today, so either can gain options. */
export interface ZstdRow extends CompressorRowBase {
  compressor: 'zstd';
}

export interface GzipRow extends CompressorRowBase {
  compressor: 'gzip';
}

export type CompressorRow = OpenZlRow | ZstdRow | GzipRow;

export function createCompressorRow(id: number, compressor: 'OpenZL'): OpenZlRow;
export function createCompressorRow(id: number, compressor: CompressorName): CompressorRow;
export function createCompressorRow(id: number, compressor: CompressorName): CompressorRow {
  const level = defaultLevelFor(compressor);
  if (compressor === 'OpenZL') {
    return {
      id,
      compressor,
      level,
      profile: DEFAULT_OPENZL_PROFILE,
      trainRequested: true,
      candidates: DEFAULT_TRAINED_CANDIDATES,
      optionsOpen: true,
    };
  }
  return {id, compressor, level};
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
