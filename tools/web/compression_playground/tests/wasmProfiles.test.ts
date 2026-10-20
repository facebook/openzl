// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {getOpenZlProfiles} from '../src/compressors.ts';
import {
  isBrowserSupportedProfile,
  isTrainableProfile,
  profileDescription,
  setWasmProfiles,
} from '../src/wasmProfiles.ts';
import {TEST_PROFILES} from './setup.ts';

describe('isBrowserSupportedProfile', () => {
  it('maps exactly the profiles the binding builds', () => {
    // The fixture includes a profile beyond the binding's convenience constants.
    expect(getOpenZlProfiles().filter(isBrowserSupportedProfile)).toEqual([
      'fixed_profile',
      'future_profile',
      'le-u16',
      'le-u32',
      'serial',
    ]);
    expect(isTrainableProfile('fixed_profile')).toBe(false);
  });

  it('lists every profile supplied by the loaded build', () => {
    expect(getOpenZlProfiles()).toHaveLength(11);
    expect(getOpenZlProfiles().filter((p) => !isBrowserSupportedProfile(p))).toHaveLength(6);
  });

  it('says why for every profile that supplies a reason', () => {
    for (const profile of TEST_PROFILES) {
      if ('wasmUnsupportedReason' in profile) {
        expect(profileDescription(profile.name)).toContain(profile.wasmUnsupportedReason);
      }
    }
  });
});

describe('profileDescription', () => {
  it('handles an unknown profile', () => {
    expect(profileDescription('unknown_profile')).toBe('');
  });

  it('handles profiles before metadata has loaded', () => {
    setWasmProfiles([]);
    try {
      expect(profileDescription('serial')).toBe('');
    } finally {
      setWasmProfiles(TEST_PROFILES);
    }
  });

  it.each([
    ['parquet', 'Cannot run in the browser: needs a custom parser graph.'],
    ['be-u32', 'Cannot run in the browser: the binding reads numbers little-endian only.'],
    ['sddl2', 'Cannot run in the browser: needs a description file, which this page cannot supply.'],
  ])('explains why %s is unpickable', (profile, reason) => {
    expect(profileDescription(profile)).toContain(`. ${reason}`);
  });

  it('leaves the description unchanged when no reason is supplied', () => {
    expect(profileDescription('unsupported_profile')).toBe('Profile unavailable in this WASM build');
  });

  it('leaves a usable profile to its own help text', () => {
    expect(profileDescription('serial')).toBe('Serial data (aka raw bytes)');
  });
});
