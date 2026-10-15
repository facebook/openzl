// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {ProfileMetadata} from '../../../wasm/js/wasm_api.js';
import type {OpenZlProfile} from './compressors.ts';

/** Metadata from the benchmark worker; looking up labels does not load WASM. */
let profiles: readonly ProfileMetadata[] = [];

let profileNames: readonly string[] = [];
let profileDescriptions: Readonly<Record<string, string>> = {};

const UNSUPPORTED_REASON = 'This profile is not supported';

export function setWasmProfiles(metadata: readonly ProfileMetadata[]): void {
  profiles = metadata;
  profileNames = metadata.map(({name}) => name).sort();
  profileDescriptions = Object.fromEntries(metadata.map(({name, description}) => [name, description]));
}

export function getOpenZlProfiles(): readonly string[] {
  return profileNames;
}

export function getOpenZlProfileDescriptions(): Readonly<Record<string, string>> {
  return profileDescriptions;
}

export function isBrowserSupportedProfile(name: string): boolean {
  return profiles.find((profile) => profile.name === name)?.isSupported === true;
}

export function isTrainableProfile(name: string): boolean {
  const profile = profiles.find((profile) => profile.name === name);
  return profile?.isSupported === true && profile.isTrainable;
}

/** The profile's help text, plus why it is unselectable when it is. */
export function profileDescription(profile: OpenZlProfile): string {
  const description = profileDescriptions[profile];
  if (description === undefined) {
    return UNSUPPORTED_REASON;
  }
  if (isBrowserSupportedProfile(profile)) {
    return description;
  }
  // Most descriptions are noun phrases with no full stop, so the reason needs
  // one supplied or the two run together as a single sentence.
  const sentence = description.endsWith('.') ? description : `${description}.`;
  return `${sentence} ${UNSUPPORTED_REASON}`;
}
