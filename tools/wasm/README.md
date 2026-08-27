# OpenZL WASM Binding

Compression in the browser for `serial` and fixed-width integers (`u8`, `i8`, `u16`, `i16`, `u32`, `i32`, `u64`, `i64`).

## How to Use

```js
import { createOpenZL, Profile } from './js/wasm_api.js'
const zl = await createOpenZL()
```

TypeScript projects use the accompanying `js/wasm_api.d.ts` declarations; the
runtime wrapper remains plain JavaScript and requires no TypeScript build step.

### 1. Serial profile – arbitrary bytes

Best for blobs, text, JSON, etc. No element-width assumption.

```js
// Input: arbitrary bytes as Uint8Array
const fileBytes = new TextEncoder().encode("hello world, hello world, ...")

// This is session-scoped and version-locked — re-fetch on page load, don't persist.
const serialCompressor = zl.getSerializedCompressor(Profile.SERIAL)
const compressed = zl.compress(fileBytes, serialCompressor)
const decompressed = zl.decompress(compressed)

console.log(new TextDecoder().decode(decompressed)) // "hello world, ..."
```

### 2. Integer profiles – fixed-width little-endian elements

The WASM layer expects **little-endian** elements of the exact width.
```js
// Input: JS numbers as u32
const values = [100, 200, 300, 400, 100, 200, 300]

// Encode to little-endian bytes: 4 bytes per element for U32
const u32LEBytes = new Uint8Array(values.length * 4)
const view = new DataView(u32LEBytes.buffer)
values.forEach((v, i) => view.setUint32(i * 4, v, /* littleEndian */ true))

// Other widths: U16 -> 2 bytes + setUint16, I32 -> setInt32, U64 -> setBigUint64, etc.
// The Profile you pick must match the width/encoding you used.
const u32Compressor = zl.getSerializedCompressor(Profile.U32)
const compressed = zl.compress(u32LEBytes, u32Compressor)
const decompressedBytes = zl.decompress(compressed)

// Decode LE bytes back to numbers
const outView = new DataView(decompressedBytes.buffer)
const roundtripped = Array.from({ length: decompressedBytes.length / 4 },
  (_, i) => outView.getUint32(i * 4, true)
)
```

### 3. Benchmark – measure throughput

`benchmark()` compresses and decompresses in a tight loop inside WASM (no JS copy overhead between steps) and reports timing.

```js
// Input:
//   data: Uint8Array to measure (e.g. fileBytes or u32LEBytes)
//   compressor: Uint8Array from getSerializedCompressor
//   iterations: optional; omitted/undefined defaults to 10
//               finite numbers are floored and clamped to
//               1..zl.maxBenchmarkIterations
//               non-numbers and non-finite values (null, NaN, Infinity) throw

// Throughput is (srcSize * iterations) / (elapsedMs / 1000) / 1e6.
// Either throughput value is Infinity when its measured duration is zero.
//
// Output: object with detailed metrics
// {
//   iterations: number,      // clamped count that actually ran
//   srcSize: number,         // input size in bytes
//   compressedSize: number,  // compressed frame size in bytes
//   ratio: number,           // srcSize / compressedSize (e.g. 2.5x)
//   compressMs: number,      // total ms for `iterations` compress calls
//   decompressMs: number,    // total ms for `iterations` decompress calls
//   compressMBps: number,    // compression throughput in MB/s
//   decompressMBps: number   // decompression throughput in MB/s
// }

const data = new TextEncoder().encode("some repeated data...".repeat(1000))
const compressor = zl.getSerializedCompressor(Profile.SERIAL)

console.log(`maximum benchmark iterations: ${zl.maxBenchmarkIterations}`)
const stats = zl.benchmark(data, compressor, 10)

// Do something with metrics
console.log(`ratio: ${stats.ratio.toFixed(2)}x`)
console.log(`compressed: ${stats.compressedSize} bytes (from ${stats.srcSize})`)
console.log(`compress: ${stats.compressMBps.toFixed(1)} MB/s over ${stats.iterations} runs`)
console.log(`decompress: ${stats.decompressMBps.toFixed(1)} MB/s`)

```
## Implementation details

* **Profiles:** The JavaScript `Profile` object is available when the module
  loads. `createOpenZL()` validates it against the C++ `kProfiles[]` table.

* **Buffer ownership:** C++ manages OpenZL handles such as `ZL_Compressor` and
  `ZL_CCtx` with `unique_ptr` deleters that call the corresponding `*_free`
  function. When returning data to JavaScript, C++ allocates a WASM buffer and
  transfers ownership to the wrapper. The wrapper copies the data into a new
  `Uint8Array`, then releases the WASM buffer with `openzl_wasm_free()`. Empty
  results still receive a one-byte allocation so C++ never returns a null
  buffer. Serializer-owned data is copied before the serializer is destroyed.

* **Session-scoped compressors:** Format version is pinned to `ZL_MAX_FORMAT_VERSION` and unstable, you should re-fetch each page load.

## Build (OSS only)

### Prerequisites

* CMake 3.20.2 or newer
* Emscripten SDK 4.0.19
* Node.js 24 to run the integration tests

```bash
git clone https://github.com/facebook/openzl.git
cd openzl
git submodule update --init --recursive
```

Install and activate the tested Emscripten version:

```bash
export OPENZL_NODE="$(command -v node)"
"$OPENZL_NODE" --version  # Must report v24.x.x
git clone https://github.com/emscripten-core/emsdk.git
(cd emsdk && ./emsdk install 4.0.19)
(cd emsdk && ./emsdk activate 4.0.19)
source emsdk/emsdk_env.sh
```

### WASM artifact

```bash
emcmake cmake -DOPENZL_BUILD_WASM=ON -B build-wasm
cmake --build build-wasm --target openzl_wasm --parallel
```

This produces:

* `build-wasm/tools/wasm/openzl.js`
* `build-wasm/tools/wasm/openzl.wasm`

OpenZL requires a 64-bit `size_t`, so the build applies `-sMEMORY64=1` to
every source file and the final link. The resulting artifact is wasm64; its
memory declaration contains `i64`, for example `(memory $0 i64 512 32768)`.

Node.js 24 supports Memory64 by default. Do not pass
`--experimental-wasm-memory64` or `--experimental-wasm-table64`. Node 24
rejects those obsolete flags.

### Test

The generated files must be next to `wasm_api.js` so its relative imports
resolve:

```bash
cp build-wasm/tools/wasm/openzl.{js,wasm} tools/wasm/js/
"$OPENZL_NODE" --test tools/wasm/js/tests/wasm_api.test.js
```
