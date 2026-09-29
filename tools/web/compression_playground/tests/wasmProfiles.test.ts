// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {OPENZL_PROFILES} from '../src/compressors.ts';
import {WASM_PROFILE, isBrowserSupportedProfile, profileDescription} from '../src/wasmProfiles.ts';

describe('WASM_PROFILE', () => {
  it('maps exactly the profiles the binding builds', () => {
    // Mirrors kProfiles in openzl_wasm.cpp: serial plus fixed-width integers.
    expect(OPENZL_PROFILES.filter(isBrowserSupportedProfile)).toEqual([
      'i8',
      'le-i16',
      'le-i32',
      'le-i64',
      'le-u16',
      'le-u32',
      'le-u64',
      'serial',
      'u8',
    ]);
  });

  it('lists every profile the CLI registers', () => {
    // 24 in `compressProfiles()`. This list is hand-copied and has gone stale
    // once already, so the count is pinned until it can be derived.
    expect(OPENZL_PROFILES).toHaveLength(24);
    expect(OPENZL_PROFILES.filter((p) => !isBrowserSupportedProfile(p))).toHaveLength(15);
  });

  it('says why for every profile it cannot run', () => {
    // `REASON_FOR` is a separate, partial table, so a profile added as `null`
    // here would otherwise be greyed out with nothing explaining it.
    const unrunnable = OPENZL_PROFILES.filter((profile) => !isBrowserSupportedProfile(profile));
    expect(unrunnable.filter((profile) => !profileDescription(profile).includes('Cannot run in the browser'))).toEqual(
      [],
    );
  });

  it('numbers them as the binding does', () => {
    // The enum values themselves are checked at compile time; this pins the
    // little-endian collapse, which the names alone do not give away.
    expect(WASM_PROFILE['le-u16']).toBe(3);
    expect(WASM_PROFILE.serial).toBe(0);
  });
});

describe('profileDescription', () => {
  it.each([
    ['parquet', /needs a custom parser graph\.$/],
    // Not a parser-graph gap: `buildProfileCompressor` hardcodes
    // `ZL_Node_interpretAsLE`, so there is no endianness to add support for.
    ['be-u32', /little-endian only\.$/],
    // Blocked at the UI, not just the binding -- there is nowhere to attach a
    // `--profile-arg` path.
    ['sddl2', /needs a description file/],
  ] as const)('gives %s its own reason for being unpickable', (profile, pattern) => {
    expect(profileDescription(profile)).toMatch(pattern);
  });

  it('leaves a usable profile to its own help text', () => {
    expect(profileDescription('serial')).toBe('Serial data (aka raw bytes)');
  });
});
