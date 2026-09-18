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

  it('numbers them as the binding does', () => {
    // The enum values themselves are checked at compile time; this pins the
    // little-endian collapse, which the names alone do not give away.
    expect(WASM_PROFILE['le-u16']).toBe(3);
    expect(WASM_PROFILE.serial).toBe(0);
  });
});

describe('profileDescription', () => {
  it('says why a profile cannot be picked', () => {
    expect(profileDescription('parquet')).toMatch(/needs a custom parser graph\.$/);
  });

  it('leaves a usable profile to its own help text', () => {
    expect(profileDescription('serial')).toBe('Serial data (aka raw bytes)');
  });
});
