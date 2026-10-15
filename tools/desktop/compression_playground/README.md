# OpenZL Compression Playground: desktop app

The playground has a browser build and an Electron desktop build. The desktop app benchmarks by running the native OpenZL CLI: it reads each `zli benchmark` result from the file `--output-csv` writes, and benchmarks the candidate files `zli train --pareto-frontier` writes. It never loads the OpenZL WASM module. zstd rows run `zstd-benchmark`, a small binary built beside `zli` against the zstd OpenZL vendors, which times zstd the way `zli benchmark` times OpenZL (one reused context, one-shot calls, the same CSV). gzip rows use Electron's built-in zlib.

The page itself is the web playground in `tools/web/compression_playground`: `yarn build:renderer` builds it with Vite into `dist/`, which the app serves.

## Development

Prerequisites are a C/C++ toolchain, CMake, and the platform requirements listed in the main OpenZL build documentation. From `tools/desktop/compression_playground`, after a `yarn install` in `tools/web` (the renderer is the web playground in `tools/web/compression_playground`):

```bash
yarn install
yarn dev
yarn test:native
yarn test:security   # needs a display (Xvfb on Linux)
yarn test:packaged   # packs the app first; needs a display
yarn test:cancel     # needs a display and ps (Linux or macOS)
yarn test:ui         # runs the sample through the page; needs a display
yarn build
```

`.github/workflows/desktop-playground-ci.yml` runs the typecheck, lint and unit tests, then builds the app on Linux, macOS and Windows and runs these smoke tests on each (cancellation on Linux and macOS only), whenever the playground, `cli/` or `tools/training/` changes.

`yarn dev` compiles `zli` and `zstd-benchmark`, starts Vite, and opens Electron. The desktop scripts run Vite with `--mode desktop`, which leaves the browser's WASM worker out of the bundle, so they need no Emscripten build of the OpenZL module. `yarn build` creates the platform installer under `release/`. ACE trains against the complete selected file (`zli train --use-all-samples --pareto-frontier`), and each candidate is then benchmarked with the requested level and iteration count. The desktop build accepts 10 to 25 candidates, the minimum `zli train` allows. OpenZL runs in ordinary `zli` processes, so its allocations do not pass through Electron's allocator. On Windows, `zli` reads its arguments in the ANSI code page, so an input whose path has characters outside it (or any character past ASCII, when the code page cannot be read or decoded) is copied into the run's scratch folder and read from there, and a temp folder with such characters is refused with a message saying which, instead of passing `zli` a mangled path.

The security boundary is deliberately small: the renderer has no Node access. An isolated preload exposes file-path resolution and one validated benchmark IPC call. Browser permission requests are denied, and only `https://openzl.org` links leave the app, opening in the system browser. The native application menu offers reload and developer tools only in unpackaged builds. The packaged renderer is served from a custom `app://bundle` origin rather than `file://`, and IPC is accepted only from that origin. The packaged binary's Electron fuses disable running as Node (`ELECTRON_RUN_AS_NODE`), `NODE_OPTIONS`, `--inspect`, and extra `file://` privileges, and only load the app from its asar archive. An Electron utility process coordinates benchmark work, while OpenZL itself runs in `zli` child processes so a native failure cannot terminate the desktop UI. Each run has its own message port: cancelling, reloading, or closing the window stops the run, and the main process kills any `zli` it started (SIGTERM, then SIGKILL) and removes the run's scratch directory, including when the utility process dies or the app quits. One benchmark runs at a time, since concurrent runs would distort each other's timings. The backend reports in the browser worker's message types (`step`, then a `result` per measurement or a `failure` per row it could not measure, then `finished` or `failed`), with trained candidates counted from 1, so one `RunTracker` (`tools/web/compression_playground/src/runTracker.ts`) turns both builds' runs into the page's state. As in the browser, a row that fails is reported and the run goes on to the next. Every input is a file already on disk, read where it is: the user's file, or the sample, `examples/getting_started/sample_inputs/era5_ints.bin`, which the installer bundles in `resources/era5_ints.bin`. `tools/web/compression_playground/src/bundledSample.json` names it for both the page and the main process. The renderer asks for the sample by name and the main process supplies its path. Every boundary is validated at runtime with zod: benchmark requests from the renderer, messages from the utility process, and the benchmark CSV `zli` writes. Unknown keys are stripped, so only validated fields travel on. The packaged app ships zod in `resources/node_modules` (copied by `electron/after-pack.cjs`), because the app itself excludes `node_modules`.

This directory is its own Yarn project, apart from the `tools/web` workspace, so the documentation build never installs Electron. It keeps its own TypeScript, ESLint and Prettier settings.
