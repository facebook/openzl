// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {getOpenZlProfiles} from '../src/compressors.ts';
import {isBrowserSupportedProfile, isTrainableProfile, profileDescription, setWasmProfiles} from '../src/wasmProfiles.ts';
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

  it('says why for every profile it cannot run', () => {
    const unrunnable = getOpenZlProfiles().filter((profile) => !isBrowserSupportedProfile(profile));
    expect(
      unrunnable.filter((profile) => !profileDescription(profile).endsWith('This profile is not supported')),
    ).toEqual([]);
  });
});

describe('profileDescription', () => {
  it('handles an unknown profile', () => {
    expect(profileDescription('unknown_profile')).toBe('This profile is not supported');
  });

  it('handles profiles before metadata has loaded', () => {
    setWasmProfiles([]);
    try {
      expect(profileDescription('serial')).toBe('This profile is not supported');
    } finally {
      setWasmProfiles(TEST_PROFILES);
    }
  });

  it.each(['parquet', 'be-u32', 'sddl2'])('explains why %s is unpickable', (profile) => {
    expect(profileDescription(profile)).toMatch(/\. This profile is not supported$/);
  });

  it('leaves a usable profile to its own help text', () => {
    expect(profileDescription('serial')).toBe('Serial data (aka raw bytes)');
  });
});
