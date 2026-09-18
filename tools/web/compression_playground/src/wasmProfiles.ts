// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {Profile, ProfileValue} from '../../../wasm/js/wasm_api.js';
import {OPENZL_PROFILE_DESCRIPTIONS, type OpenZlProfile} from './compressors.ts';

/**
 * The UI lists the CLI's profiles; the WASM binding implements nine of them
 * (`kProfiles` in openzl_wasm.cpp). The rest need a custom parser graph, still
 * a TODO on `buildProfileCompressor`, so they map to null and cannot run here.
 *
 * `satisfies` ties each value to the binding's own enum, so renumbering
 * `kProfiles` breaks the build rather than benchmarking the wrong type. The
 * reference stays type-only on purpose: importing `Profile` as a value would
 * make Vite resolve its dynamic `import('./openzl.js')`, a build artifact.
 *
 * The binding assumes little-endian, hence `le-u16` collapsing to `U16`.
 */
export const WASM_PROFILE: Record<OpenZlProfile, ProfileValue | null> = {
  serial: 0 satisfies (typeof Profile)['SERIAL'],
  u8: 1 satisfies (typeof Profile)['U8'],
  i8: 2 satisfies (typeof Profile)['I8'],
  'le-u16': 3 satisfies (typeof Profile)['U16'],
  'le-i16': 4 satisfies (typeof Profile)['I16'],
  'le-u32': 5 satisfies (typeof Profile)['U32'],
  'le-i32': 6 satisfies (typeof Profile)['I32'],
  'le-u64': 7 satisfies (typeof Profile)['U64'],
  'le-i64': 8 satisfies (typeof Profile)['I64'],
  csv: null,
  lz: null,
  parquet: null,
  pytorch: null,
  zstd: null,
};

export function isBrowserSupportedProfile(profile: OpenZlProfile): boolean {
  return WASM_PROFILE[profile] !== null;
}

/** The profile's help text, plus why it is unselectable when it is. */
export function profileDescription(profile: OpenZlProfile): string {
  const description = OPENZL_PROFILE_DESCRIPTIONS[profile];
  return isBrowserSupportedProfile(profile)
    ? description
    : `${description} Cannot run in the browser: needs a custom parser graph.`;
}
