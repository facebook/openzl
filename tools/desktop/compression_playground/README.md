# OpenZL Compression Playground: desktop app

The playground has a browser build and an Electron desktop build. The desktop app benchmarks by running the native OpenZL CLI: it reads each `zli benchmark` result from the file `--output-csv` writes, and benchmarks the candidate files `zli train --pareto-frontier` writes. It never loads the OpenZL WASM module. zstd rows run `zstd-benchmark`, a small binary built beside `zli` against the zstd OpenZL vendors, which times zstd the way `zli benchmark` times OpenZL (one reused context, one-shot calls, the same CSV). gzip rows use Electron's built-in zlib.

The page itself is the web playground in `tools/web/compression_playground`: `yarn build:renderer` builds it with Vite into `dist/`, which the app serves.

## Development

Prerequisites are a C/C++ toolchain, CMake, and the platform requirements listed in the main OpenZL build documentation. The renderer is the web playground in `tools/web/compression_playground`, and its Vite comes from the `tools/web` workspace, so that needs its own install. From `tools/desktop/compression_playground`:

```bash
yarn --cwd ../../web install
yarn install
yarn dev
yarn test:native
yarn test:security   # needs a display (Xvfb on Linux)
yarn test:packaged   # packs the app first; needs a display
yarn test:cancel     # needs a display and ps (Linux or macOS)
yarn build
```

On Linux, Electron needs the shared libraries Chromium uses, and a minimal install or WSL may not have all of them. If Electron exits with `error while loading shared libraries: libasound.so.2`, install ALSA's library: on Debian and Ubuntu, `sudo apt install libasound2t64` (`libasound2` before Ubuntu 24.04). Install any other library it names the same way.

On Windows, OpenZL builds with clang-cl, as `windows-ci.yml` does, not MSVC's `cl`. Install Visual Studio's "C++ Clang tools for Windows" component and select the toolset before the first native build (CMake only reads `CMAKE_GENERATOR_TOOLSET` when `CMAKE_GENERATOR` is set too, so name your Visual Studio version); delete a build directory already configured for MSVC first. Some dependencies' build paths exceed Windows' path length limit, so check out to a short path such as `C:\oz`. In PowerShell:

```powershell
$env:CMAKE_GENERATOR = 'Visual Studio 17 2022'
$env:CMAKE_GENERATOR_TOOLSET = 'ClangCL'
Remove-Item -Recurse -Force electron\native\build -ErrorAction SilentlyContinue
yarn dev
```

`yarn dev` compiles `zli` and `zstd-benchmark`, starts Vite, and opens Electron. `yarn build` creates the platform installer under `release/`. ACE trains against the complete selected file (`zli train --use-all-samples --pareto-frontier`), and each candidate is then benchmarked with the requested level and iteration count. The desktop build accepts 10 to 25 candidates, the minimum `zli train` allows. OpenZL runs in ordinary `zli` processes, so its allocations do not pass through Electron's allocator. On Windows, `zli` reads its arguments in the ANSI code page, so an input path or temp folder with characters outside it is refused with a message saying which, instead of passing `zli` a mangled path. Which characters a code page has comes from `iconv-lite`'s Windows tables.

The security boundary is deliberately small: the renderer has no Node access. An isolated preload exposes file-path resolution and one validated benchmark IPC call. Browser permission requests are denied, and only `https://openzl.org` links leave the app, opening in the system browser. The packaged renderer is served from a custom `app://bundle` origin rather than `file://`, and IPC is accepted only from that origin. The packaged binary's Electron fuses disable running as Node (`ELECTRON_RUN_AS_NODE`), `NODE_OPTIONS`, `--inspect`, and extra `file://` privileges, and only load the app from its asar archive. An Electron utility process coordinates benchmark work, while OpenZL itself runs in `zli` child processes so a native failure cannot terminate the desktop UI. Each run has its own message port: cancelling, reloading, or closing the window stops the run, and the main process kills any `zli` it started (SIGTERM, then SIGKILL) and removes the run's scratch directory, including when the utility process dies or the app quits. One benchmark runs at a time, since concurrent runs would distort each other's timings. Inputs without a file path are copied into memory. Every boundary is validated at runtime with zod: benchmark requests from the renderer, messages from the utility process, and the benchmark CSV `zli` writes. Unknown keys are stripped, so only validated fields travel on. The packaged app ships zod, and `iconv-lite` with its `safer-buffer`, in `resources/node_modules` (copied by `electron/after-pack.cjs`), because the app itself excludes `node_modules`.

The backend streams in the browser worker's message types: `step` with each fraction `zli train`'s progress bar shows and as each trained candidate starts, a `result` per measurement as soon as it is made, or a `failure` for a row it could not measure, then `finished`, `failed`, or the desktop-only `cancelled`. Trained candidates are counted from 1, best ratio first, and `candidate` is null for every row that does not train. As in the browser, a row that fails is reported and the run goes on to the next; only cancelling ends it early.

This directory is its own Yarn project, apart from the `tools/web` workspace, so the documentation build never installs Electron. It keeps its own TypeScript, ESLint and Prettier settings.
