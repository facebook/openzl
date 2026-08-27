// Copyright (c) Meta Platforms, Inc. and affiliates.

// Defined at module load and validated against the native table by createOpenZL().
export const Profile = Object.freeze({
  SERIAL: 0,
  U8: 1,
  I8: 2,
  U16: 3,
  I16: 4,
  U32: 5,
  I32: 6,
  U64: 7,
  I64: 8,
});

// The build sets -sMEMORY64=1, so size_t and pointers are 8 bytes.
const SIZE_T_BYTES = 8;
const PTR_BYTES = 8;
const DOUBLE_BYTES = 8;

const DEFAULT_BENCHMARK_ITERATIONS = 10;

function clampIterations(n, maxIterations) {
  if (typeof n !== 'number' || !Number.isFinite(n)) {
    throw new Error(
      `benchmark iterations must be a finite number, got ${String(n)} — expected 1..${maxIterations}`,
    );
  }
  return Math.min(Math.max(1, Math.floor(n)), maxIterations);
}

const MS_PER_S = 1000;
const BYTES_PER_MB = 1000 * 1000;

// Convert total bytes and elapsed milliseconds to decimal MB/s. A zero
// duration produces Infinity.
function mbPerSec(bytes, iterations, ms) {
  return ms > 0
    ? (bytes * iterations) / (ms / MS_PER_S) / BYTES_PER_MB
    : Infinity;
}

// ABI ownership: JS allocates and frees input buffers and out-parameter
// storage, while C borrows inputs for one call. C transfers byte-buffer
// outputs to JS, which copies and frees them; borrowed strings are copied
// with UTF8ToString. No raw pointer leaves this wrapper.
function createMemory(mod) {
  // -sMEMORY64=1 makes every pointer, size and count in the C ABI an i64. JS
  // has one ordinary numeric type (Number), so it cannot represent an arbitrary i64.
  // The spec had to pick a mapping, and -sWASM_BIGINT=1 picks BigInt. Here we convert to BigInt.
  const toWasm64 = (n) => BigInt(n);

  const malloc = (bytes) => {
    const ptr = Number(mod._openzl_wasm_malloc(toWasm64(bytes)));
    if (!ptr) {
      throw new Error('wasm malloc failed');
    }
    return ptr;
  };
  const free = (ptr) => mod._openzl_wasm_free(toWasm64(ptr));

  return {
    toWasm64,
    malloc,
    free,

    writeBytes(data) {
      if (data.length === 0) {
        // malloc(0) may return 0, hand back a non-null scratch
        // so C null-checks (srcSize==0) succeed and free() stays safe.
        return malloc(1);
      }
      const ptr = malloc(data.length);
      mod.HEAPU8.set(data, ptr);
      return ptr;
    },

    readBytes(ptr, len) {
      // Copies out of wasm memory into the JS heap
      return mod.HEAPU8.slice(ptr, ptr + len);
    },

    // Reads a pointer-width out-param as a Number. WASM build is wasm64-only
    // (OpenZL static-asserts sizeof(size_t)==8), so HEAPU64 must exist.
    readPointer(ptr) {
      if (!mod.HEAPU64) {
        throw new Error(
          'HEAPU64 is missing — expected wasm64 build with -sMEMORY64=1 and HEAPU64 in EXPORTED_RUNTIME_METHODS. wasm32 is not supported (sizeof(size_t)==8 static assert).',
        );
      }
      return Number(mod.HEAPU64[Math.floor(ptr / 8)]);
    },

    // A size_t is pointer-width
    readSizeT(ptr) {
      return this.readPointer(ptr);
    },

    readDouble(ptr) {
      if (!mod.HEAPF64) {
        throw new Error(
          'HEAPF64 is missing — expected HEAPF64 in EXPORTED_RUNTIME_METHODS.',
        );
      }
      return mod.HEAPF64[Math.floor(ptr / 8)];
    },

    errorString(code) {
      // Returns a pointer since c hands ptr
      const ptr = Number(mod._openzl_wasm_errorString(code));
      return ptr ? mod.UTF8ToString(ptr) : `unknown error ${code}`;
    },
  };
}

function throwWasmError(mem, code, fallback) {
  const err = new Error(code !== 0 ? mem.errorString(code) : fallback);
  err.code = code;
  throw err;
}

function loadProfiles(mod) {
  const profiles = {};
  for (let value = 0; ; value++) {
    // wasm64 pointers are returned as bigint, Emscripten helpers use Number offsets.
    const namePtr = Number(mod._openzl_wasm_profileName(value));
    if (!namePtr) {
      return Object.freeze(profiles);
    }
    profiles[mod.UTF8ToString(namePtr).toUpperCase()] = value;
  }
}

function profileTablesMatch(left, right) {
  const names = Object.keys(left);
  return (
    names.length === Object.keys(right).length &&
    names.every((name) => left[name] === right[name])
  );
}

function verifyProfiles(profiles) {
  if (Object.keys(profiles).length === 0) {
    throw new Error('OpenZL WASM module exposed an empty profile table');
  }
  if (!profileTablesMatch(Profile, profiles)) {
    throw new Error(
      `OpenZL WASM profile table does not match the Profile export: expected ${JSON.stringify(Profile)}, got ${JSON.stringify(profiles)}`,
    );
  }
}

export async function createOpenZL(options = {}) {
  let openzlModule;
  try {
    ({ default: openzlModule } = await import('./openzl.js'));
  } catch (e) {
    const msg = e?.message ?? String(e);
    const isNotFound =
      e?.code === 'ERR_MODULE_NOT_FOUND' ||
      /Failed to fetch dynamically imported module|error loading dynamically imported module|Importing a module script failed/i.test(
        msg,
      );
    if (isNotFound) {
      throw new Error(
        'openzl.js not found next to wasm_api.js — build with: emcmake cmake -DOPENZL_BUILD_WASM=ON -B build-wasm && cmake --build build-wasm --target openzl_wasm && cp build-wasm/tools/wasm/openzl.{js,wasm} tools/wasm/js/ (and tools/visualization_app/public/ for the viz app). Original: ' +
          msg,
        { cause: e },
      );
    }
    throw new Error(`Failed to load openzl.js: ${msg}`, { cause: e });
  }
  const {
    wasmUrl = new URL('./openzl.wasm', import.meta.url).href,
    locateFile: customLocateFile,
    ...moduleOptions
  } = options;
  const locateFile =
    customLocateFile ??
    ((path, prefix = '') =>
      path.endsWith('.wasm') ? wasmUrl : `${prefix}${path}`);
  const mod = await openzlModule({ ...moduleOptions, locateFile });
  const maxBenchmarkIterations = mod._openzl_wasm_maxBenchmarkIterations();
  if (!Number.isInteger(maxBenchmarkIterations) || maxBenchmarkIterations < 1) {
    throw new Error(
      `OpenZL WASM module exposed an invalid maximum benchmark iteration count: ${String(maxBenchmarkIterations)}`,
    );
  }
  const profiles = loadProfiles(mod);
  verifyProfiles(profiles);
  const mem = createMemory(mod);
  const compressorCache = new Map();

  return {
    get maxBenchmarkIterations() {
      return maxBenchmarkIterations;
    },

    // Returns the serialized compressor for a built-in profile,
    // cached per-profile (copy-on-read) so repeated calls reuse it.
    // Exposed since we want users to be able to download compressors.
    getSerializedCompressor(profile) {
      if (!Object.values(profiles).includes(profile)) {
        throw new Error(
          `unknown profile ${profile}; use one of the Profile constants`,
        );
      }
      const cached = compressorCache.get(profile);
      if (cached) {
        return cached.slice();
      }
      let outBuf = 0;
      let outSize = 0;
      let bufPtr = 0;
      try {
        // C writes its result through the buffer address and its length.
        outBuf = mem.malloc(PTR_BYTES);
        outSize = mem.malloc(SIZE_T_BYTES);
        const code = mod._openzl_wasm_getSerializedCompressor(
          profile,
          mem.toWasm64(outBuf),
          mem.toWasm64(outSize),
        );
        if (code !== 0) {
          throwWasmError(
            mem,
            code,
            `failed to build compressor for profile ${profile}`,
          );
        }
        // On success outBuf is non-NULL even for a zero-length result.
        bufPtr = mem.readPointer(outBuf);
        const bytes = mem.readBytes(bufPtr, mem.readSizeT(outSize));
        compressorCache.set(profile, bytes.slice());
        return bytes;
      } finally {
        mem.free(bufPtr);
        mem.free(outBuf);
        mem.free(outSize);
      }
    },

    compress(data, compressor) {
      if (!(data instanceof Uint8Array)) {
        throw new Error('compress expects Uint8Array');
      }
      if (!(compressor instanceof Uint8Array)) {
        throw new Error('compress expects a serialized compressor Uint8Array');
      }
      let compPtr = 0;
      let srcPtr = 0;
      let outBuf = 0;
      let outSize = 0;
      let bufPtr = 0;
      try {
        compPtr = mem.writeBytes(compressor);
        srcPtr = mem.writeBytes(data);
        outBuf = mem.malloc(PTR_BYTES);
        outSize = mem.malloc(SIZE_T_BYTES);
        const code = mod._openzl_wasm_compress(
          mem.toWasm64(compPtr),
          mem.toWasm64(compressor.length),
          mem.toWasm64(srcPtr),
          mem.toWasm64(data.length),
          mem.toWasm64(outBuf),
          mem.toWasm64(outSize),
        );
        if (code !== 0) {
          throwWasmError(mem, code, 'compress failed');
        }
        bufPtr = mem.readPointer(outBuf);
        return mem.readBytes(bufPtr, mem.readSizeT(outSize));
      } finally {
        mem.free(bufPtr);
        mem.free(compPtr);
        mem.free(srcPtr);
        mem.free(outBuf);
        mem.free(outSize);
      }
    },

    decompress(compressed) {
      if (!(compressed instanceof Uint8Array)) {
        throw new Error('decompress expects Uint8Array');
      }
      let srcPtr = 0;
      let outBuf = 0;
      let outSize = 0;
      let bufPtr = 0;
      try {
        srcPtr = mem.writeBytes(compressed);
        outBuf = mem.malloc(PTR_BYTES);
        outSize = mem.malloc(SIZE_T_BYTES);
        const code = mod._openzl_wasm_decompress(
          mem.toWasm64(srcPtr),
          mem.toWasm64(compressed.length),
          mem.toWasm64(outBuf),
          mem.toWasm64(outSize),
        );
        if (code !== 0) {
          throwWasmError(mem, code, 'decompress failed');
        }
        bufPtr = mem.readPointer(outBuf);
        return mem.readBytes(bufPtr, mem.readSizeT(outSize));
      } finally {
        mem.free(bufPtr);
        mem.free(srcPtr);
        mem.free(outBuf);
        mem.free(outSize);
      }
    },

    benchmark(data, compressor, iterations = DEFAULT_BENCHMARK_ITERATIONS) {
      if (!(data instanceof Uint8Array)) {
        throw new Error('benchmark expects Uint8Array');
      }
      if (!(compressor instanceof Uint8Array)) {
        throw new Error('benchmark expects a serialized compressor Uint8Array');
      }
      // C rejects a count outside its range rather than clamping, so clamp
      // before asking.
      const runs = clampIterations(iterations, maxBenchmarkIterations);
      let compPtr = 0;
      let srcPtr = 0;
      let outFrame = 0;
      let outFrameSize = 0;
      let outCompressMs = 0;
      let framePtr = 0;
      let outDecompressMs = 0;
      try {
        compPtr = mem.writeBytes(compressor);
        srcPtr = mem.writeBytes(data);
        // Addresses for C to write its results through, one per out-param.
        outFrame = mem.malloc(PTR_BYTES);
        outFrameSize = mem.malloc(SIZE_T_BYTES);
        outCompressMs = mem.malloc(DOUBLE_BYTES);
        const code = mod._openzl_wasm_benchmarkCompress(
          mem.toWasm64(compPtr),
          mem.toWasm64(compressor.length),
          mem.toWasm64(srcPtr),
          mem.toWasm64(data.length),
          mem.toWasm64(runs),
          mem.toWasm64(outFrame),
          mem.toWasm64(outFrameSize),
          mem.toWasm64(outCompressMs),
        );
        if (code !== 0) {
          throwWasmError(mem, code, 'benchmark compress failed');
        }
        // Capture ownership before subsequent result reads can throw, so the
        // frame is always released by the finally block.
        framePtr = mem.readPointer(outFrame);
        const compressMs = mem.readDouble(outCompressMs);
        const compressedSize = mem.readSizeT(outFrameSize);

        // Compression left its frame in wasm memory, so decompression takes
        // that pointer directly.
        outDecompressMs = mem.malloc(DOUBLE_BYTES);
        const decompressCode = mod._openzl_wasm_benchmarkDecompress(
          mem.toWasm64(framePtr),
          mem.toWasm64(compressedSize),
          mem.toWasm64(runs),
          mem.toWasm64(outDecompressMs),
        );
        if (decompressCode !== 0) {
          throwWasmError(mem, decompressCode, 'benchmark decompress failed');
        }
        const decompressMs = mem.readDouble(outDecompressMs);

        return {
          iterations: runs,
          srcSize: data.length,
          compressedSize,
          ratio: compressedSize > 0 ? data.length / compressedSize : 0,
          compressMs, // elapsed time in milliseconds
          decompressMs,
          compressMBps: mbPerSec(data.length, runs, compressMs), // throughput in megabytes per second
          decompressMBps: mbPerSec(data.length, runs, decompressMs),
        };
      } finally {
        mem.free(framePtr);
        mem.free(outDecompressMs);
        mem.free(compPtr);
        mem.free(srcPtr);
        mem.free(outFrame);
        mem.free(outFrameSize);
        mem.free(outCompressMs);
      }
    },
  };
}
