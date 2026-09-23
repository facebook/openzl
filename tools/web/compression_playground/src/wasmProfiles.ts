// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {Profile, ProfileValue} from '../../../wasm/js/wasm_api.js';
import {OPENZL_PROFILE_DESCRIPTIONS, type OpenZlProfile} from './compressors.ts';

/**
 * The UI lists the CLI's profiles; the WASM binding implements nine of them
 * (`kProfiles` in openzl_wasm.cpp). The other fifteen map to null and cannot
 * run here, for reasons that differ -- see `REASON_FOR` below.
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
  'be-i16': null,
  'be-i32': null,
  'be-i64': null,
  'be-u16': null,
  'be-u32': null,
  'be-u64': null,
  csv: null,
  lz: null,
  'numeric-ml-selector-64': null,
  parquet: null,
  pytorch: null,
  sao: null,
  sddl: null,
  sddl2: null,
  zstd: null,
};

/**
 * Why a profile cannot run here, for the ones that cannot. Kept apart from
 * `WASM_PROFILE` because the reasons are not interchangeable: saying a
 * big-endian profile needs a parser graph would send someone off to write one,
 * when the binding has no endianness to fix.
 */
// Not annotated `Record<string, string>`: that would make a misspelled key
// below type-check and render as "undefined" in the tooltip.
const UNSUPPORTED_REASON = {
  BIG_ENDIAN: 'Cannot run in the browser: the binding reads numbers little-endian only.',
  PARSER_GRAPH: 'Cannot run in the browser: needs a custom parser graph.',
  DESCRIPTION_FILE: 'Cannot run in the browser: needs a description file, which this page cannot supply.',
  UNIMPLEMENTED: 'Cannot run in the browser: the binding does not implement it.',
} as const;

const REASON_FOR: Partial<Record<OpenZlProfile, string>> = {
  'be-i16': UNSUPPORTED_REASON.BIG_ENDIAN,
  'be-i32': UNSUPPORTED_REASON.BIG_ENDIAN,
  'be-i64': UNSUPPORTED_REASON.BIG_ENDIAN,
  'be-u16': UNSUPPORTED_REASON.BIG_ENDIAN,
  'be-u32': UNSUPPORTED_REASON.BIG_ENDIAN,
  'be-u64': UNSUPPORTED_REASON.BIG_ENDIAN,
  csv: UNSUPPORTED_REASON.PARSER_GRAPH,
  lz: UNSUPPORTED_REASON.PARSER_GRAPH,
  parquet: UNSUPPORTED_REASON.PARSER_GRAPH,
  pytorch: UNSUPPORTED_REASON.PARSER_GRAPH,
  sao: UNSUPPORTED_REASON.PARSER_GRAPH,
  zstd: UNSUPPORTED_REASON.PARSER_GRAPH,
  sddl: UNSUPPORTED_REASON.DESCRIPTION_FILE,
  sddl2: UNSUPPORTED_REASON.DESCRIPTION_FILE,
  'numeric-ml-selector-64': UNSUPPORTED_REASON.UNIMPLEMENTED,
};

export function isBrowserSupportedProfile(profile: OpenZlProfile): boolean {
  return WASM_PROFILE[profile] !== null;
}

/** The profile's help text, plus why it is unselectable when it is. */
export function profileDescription(profile: OpenZlProfile): string {
  const description = OPENZL_PROFILE_DESCRIPTIONS[profile];
  const reason = REASON_FOR[profile];
  if (reason === undefined) {
    return description;
  }
  // Most descriptions are noun phrases with no full stop, so the reason needs
  // one supplied or the two run together as a single sentence.
  const sentence = description.endsWith('.') ? description : `${description}.`;
  return `${sentence} ${reason}`;
}
