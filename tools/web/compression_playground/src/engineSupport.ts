// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * What the current browser can do with the OpenZL WASM module.
 *
 * Two gates, and they fail differently:
 *
 * - `memory64`. OpenZL assumes a 64-bit `size_t`: `common/allocation.c` asserts
 *   it outright, and the entropy coder sizes its bit container off it
 *   (`ZS_BITSTREAM_WRITE_MAX_BITS` in `codecs/common/bitstream/ff_bitstream.h`).
 *   So the module is built wasm64 and there is no 32-bit build to fall back to.
 *   Without Memory64 it cannot instantiate at all.
 * - `threads`. Only the trainers fan out over a thread pool; compress,
 *   decompress and benchmark are single-threaded. This gates training alone, so
 *   a browser without it can still run the benchmark the tool exists for.
 *
 * Checking both before loading matters because the alternative is a bare
 * instantiation error that says nothing about which gate was missed.
 */
export interface EngineSupport {
  /** WebAssembly at all. False only on browsers old enough that the rest is moot. */
  wasm: boolean;
  /** The engine accepts a 64-bit memory. Safari is the browser that fails this. */
  memory64: boolean;
  /** Training only. Needs COOP/COEP response headers, which GitHub Pages cannot set. */
  threads: boolean;
}

/** Why the module cannot load. `null` when it can. */
export type EngineBlocker = 'no-wasm' | 'no-memory64';

/**
 * The smallest module that an engine can only accept with Memory64: a memory
 * section declaring one memory whose limits carry the 64-bit flag.
 *
 * Byte for byte:
 *
 * | `00 61 73 6d` | magic                                       |
 * | `01 00 00 00` | version 1                                   |
 * | `05`          | section 5, memory                           |
 * | `03`          | section length                              |
 * | `01`          | one memory                                  |
 * | `04`          | limits flags: bit 2 set, 64-bit index type  |
 * | `01`          | minimum one page                            |
 *
 * Only the flags byte separates this from the wasm32 equivalent (`00`), so it
 * isolates the one feature being probed.
 */
const WASM64_MEMORY_MODULE = new Uint8Array([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x05, 0x03, 0x01, 0x04, 0x01,
]);

export function detectEngineSupport(): EngineSupport {
  const wasm = typeof globalThis.WebAssembly !== 'undefined';
  return {
    wasm,
    // Guarded rather than probed unconditionally: without WebAssembly the probe
    // would throw reading `validate` off undefined rather than return false.
    memory64: wasm && supportsMemory64(),
    threads: supportsThreads(),
  };
}

/**
 * Probes by decoding a module rather than by constructing a
 * `WebAssembly.Memory`, because the descriptor route cannot report absence.
 *
 * `MemoryDescriptor` is a WebIDL dictionary, and WebIDL ignores members it does
 * not recognise instead of rejecting them. An engine without Memory64 therefore
 * accepts a descriptor carrying the 64-bit key, quietly builds an ordinary
 * 32-bit memory, and the probe reads as support. The key has also already been
 * renamed once (`index` to `address`, with values `i32` and `i64`), so a
 * descriptor probe can silently pass on every engine if it names the old one.
 *
 * `validate` has neither problem: the flag lives in the module encoding, which
 * an engine must either decode or reject.
 */
function supportsMemory64(): boolean {
  return globalThis.WebAssembly.validate(WASM64_MEMORY_MODULE);
}

function supportsThreads(): boolean {
  // Both, not either. The constructor can be exposed while the document is not
  // isolated, and in that state the module fails later, during instantiation.
  return typeof globalThis.SharedArrayBuffer !== 'undefined' && globalThis.crossOriginIsolated === true;
}

/**
 * The first gate that stops the module loading, or `null` if it will load.
 *
 * Ordered so the more fundamental failure wins: a browser with no WebAssembly
 * also reports no Memory64, and that is the less useful of the two to say.
 */
export function engineBlocker(support: EngineSupport): EngineBlocker | null {
  if (!support.wasm) {
    return 'no-wasm';
  }
  if (!support.memory64) {
    return 'no-memory64';
  }
  return null;
}
