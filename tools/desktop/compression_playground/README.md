# OpenZL Compression Playground: desktop app

The playground has a browser build and an Electron desktop build. The desktop app benchmarks by running the native OpenZL CLI: it reads each `zli benchmark` result from the file `--output-csv` writes, and benchmarks the candidate files `zli train --pareto-frontier` writes. It never loads the OpenZL WASM module. Native zstd and gzip bindings are used for comparison rows.

The page itself is the web playground in `tools/web/compression_playground`: `yarn build:renderer` builds it with Vite into `dist/`, which the app serves.

## Development

Prerequisites are a C/C++ toolchain, CMake, and the platform requirements listed in the main OpenZL build documentation. From `tools/desktop/compression_playground`, after a `yarn install` in `tools/web` (the renderer is the web playground in `tools/web/compression_playground`):

```bash
yarn install
yarn dev
yarn test:native
yarn test:security   # needs a display (Xvfb on Linux)
yarn build
```

`yarn dev` compiles `zli`, starts Vite, and opens Electron. `yarn build` creates the platform installer under `release/`. ACE trains against the complete selected file (`zli train --use-all-samples --pareto-frontier`), and each candidate is then benchmarked with the requested level and iteration count. The desktop build accepts 10 to 25 candidates, the minimum `zli train` allows. OpenZL runs in ordinary `zli` processes, so its allocations do not pass through Electron's allocator.

The security boundary is deliberately small: the renderer has no Node access. An isolated preload exposes file-path resolution and one validated benchmark IPC call. Browser permission requests are denied, and only `https://openzl.org` links leave the app, opening in the system browser. An Electron utility process coordinates benchmark work, while OpenZL itself runs in `zli` child processes so a native failure cannot terminate the desktop UI.

This directory is its own Yarn project, apart from the `tools/web` workspace, so the documentation build never installs Electron. It keeps its own TypeScript, ESLint and Prettier settings.
