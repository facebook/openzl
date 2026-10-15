// Copyright (c) Meta Platforms, Inc. and affiliates.

// jsdom omits several DOM APIs the Chakra/zag machines rely on to position
// overlays and scroll the selected option into view. Browsers provide all of
// these; the stubs below only exist so interaction tests can drive the real
// components under jsdom.
// Guarded because this file is also loaded for the node-environment suites,
// where `Element` does not exist at all.
if (typeof Element !== 'undefined') {
  Element.prototype.scrollTo = () => undefined;
  Element.prototype.scrollIntoView = () => undefined;
}

class ResizeObserverStub {
  observe() {
    return undefined;
  }
  unobserve() {
    return undefined;
  }
  disconnect() {
    return undefined;
  }
}
globalThis.ResizeObserver ??= ResizeObserverStub as unknown as typeof ResizeObserver;

// Real WASM enumeration is covered by tools/wasm/js/tests/wasm_api.test.js.
import {vi} from 'vitest';
import type {ProfileMetadata} from '../../../wasm/js/wasm_api.js';
import {setWasmProfiles} from '../src/wasmProfiles.ts';
export const TEST_PROFILES = [
  {name: 'serial', description: 'Serial data (aka raw bytes)', isSupported: true, isTrainable: true},
  {name: 'le-u16', description: 'Little-endian unsigned 16-bit data', isSupported: true, isTrainable: true},
  {name: 'le-u32', description: 'Little-endian unsigned 32-bit data', isSupported: true, isTrainable: true},
  {
    name: 'future_profile',
    description: 'Profile supplied by the WASM build',
    isSupported: true,
    isTrainable: true,
  },
  {name: 'fixed_profile', description: 'Training is not supported', isSupported: true, isTrainable: false},
  {
    name: 'unsupported_profile',
    description: 'Profile unavailable in this WASM build',
    isSupported: false,
    isTrainable: false,
  },
  {name: 'be-u32', description: 'Big-endian unsigned 32-bit data', isSupported: false, isTrainable: false},
  {name: 'csv', description: 'CSV data', isSupported: false, isTrainable: false},
  {name: 'parquet', description: 'Parquet data', isSupported: false, isTrainable: false},
  {name: 'pytorch', description: 'Pytorch model', isSupported: false, isTrainable: false},
  {name: 'sddl2', description: 'SDDL v2 data', isSupported: false, isTrainable: false},
] satisfies readonly ProfileMetadata[];
setWasmProfiles(TEST_PROFILES);
vi.mock('../src/runBenchmark.ts', async (importOriginal) => ({
  ...(await importOriginal<typeof import('../src/runBenchmark.ts')>()),
  getBenchmarkProfiles: vi.fn(async () => TEST_PROFILES),
}));
