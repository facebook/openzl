// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, it, expect, afterEach, vi} from 'vitest';
import {detectEngineSupport, engineBlocker} from '../src/engineSupport.ts';

/**
 * The Node runtime this suite runs under supports Memory64, so the real engine
 * validates the wasm64 module and the unsupported path is unreachable without a
 * stand-in.
 */
function fakeWebAssembly(acceptsMemory64: boolean) {
  return {validate: () => acceptsMemory64};
}

/**
 * These run against the real engine, not a stub, because the bug a stub cannot
 * catch is the probe asking the engine a question it does not understand.
 *
 * The first version of this check constructed
 * `new WebAssembly.Memory({initial: 1, maximum: 1, index: 'u64'})`. No engine
 * rejects that: `MemoryDescriptor` is a WebIDL dictionary, so an unrecognised
 * member is ignored rather than refused, and the key had in any case been
 * renamed to `address` with values `i32` and `i64`. The probe returned true
 * everywhere, including on Safari, and every test passed because every test
 * stubbed the constructor it was wrong about.
 */
describe('the probe itself', () => {
  it('asks the engine something it actually decodes', () => {
    // Flipping the 64-bit flag to a value no engine defines must be rejected.
    // If this passed, `validate` would be rubber-stamping and the probe would
    // be a no-op that always reports support.
    const bogusFlags = new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x05, 0x03, 0x01, 0x40, 0x01]);
    expect(WebAssembly.validate(bogusFlags)).toBe(false);
    expect(WebAssembly.validate(new Uint8Array([1, 2, 3, 4]))).toBe(false);
  });

  it('accepts the wasm32 counterpart on every engine', () => {
    // Same module as the probe's, with the limits flags byte cleared. It must
    // pass everywhere, which pins the wasm64 result on the flag alone rather
    // than on some unrelated malformation.
    const wasm32 = new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x05, 0x03, 0x01, 0x00, 0x01]);
    expect(WebAssembly.validate(wasm32)).toBe(true);
  });

  it('reports this engine honestly', () => {
    // The Node runtime this suite runs under ships Memory64. If the probe ever
    // regresses to a form the engine ignores, this keeps passing, which is why
    // the rejection case above is the load-bearing one.
    expect(detectEngineSupport().memory64).toBe(true);
  });
});

describe('detectEngineSupport', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('reports memory64 when the engine accepts a 64-bit memory', () => {
    vi.stubGlobal('WebAssembly', fakeWebAssembly(true));
    expect(detectEngineSupport().memory64).toBe(true);
  });

  it('reports no memory64 when the engine rejects the module', () => {
    // Safari's failure mode: WebAssembly is present, Memory64 is not.
    vi.stubGlobal('WebAssembly', fakeWebAssembly(false));
    expect(detectEngineSupport()).toMatchObject({wasm: true, memory64: false});
  });

  it('does not probe memory64 when WebAssembly is missing entirely', () => {
    // The probe would throw reading `validate` off undefined.
    vi.stubGlobal('WebAssembly', undefined);
    expect(detectEngineSupport()).toMatchObject({wasm: false, memory64: false});
  });

  it('requires both SharedArrayBuffer and cross-origin isolation for threads', () => {
    vi.stubGlobal('WebAssembly', fakeWebAssembly(true));

    vi.stubGlobal('SharedArrayBuffer', undefined);
    vi.stubGlobal('crossOriginIsolated', true);
    expect(detectEngineSupport().threads).toBe(false);

    // The case the second half of the guard exists for: the constructor is
    // exposed, but the document is not isolated.
    vi.stubGlobal('SharedArrayBuffer', ArrayBuffer);
    vi.stubGlobal('crossOriginIsolated', false);
    expect(detectEngineSupport().threads).toBe(false);

    vi.stubGlobal('SharedArrayBuffer', ArrayBuffer);
    vi.stubGlobal('crossOriginIsolated', true);
    expect(detectEngineSupport().threads).toBe(true);
  });

  it('treats missing threads as a training limit, not a load failure', () => {
    vi.stubGlobal('WebAssembly', fakeWebAssembly(true));
    vi.stubGlobal('SharedArrayBuffer', undefined);
    const support = detectEngineSupport();
    expect(support.threads).toBe(false);
    expect(engineBlocker(support)).toBeNull();
  });
});

describe('engineBlocker', () => {
  it('reports the missing 64-bit memory', () => {
    expect(engineBlocker({wasm: true, memory64: false, threads: false})).toBe('no-memory64');
  });

  it('prefers the absent runtime over the absent feature', () => {
    expect(engineBlocker({wasm: false, memory64: false, threads: false})).toBe('no-wasm');
  });

  it('reports nothing when the module can load', () => {
    expect(engineBlocker({wasm: true, memory64: true, threads: false})).toBeNull();
  });
});
