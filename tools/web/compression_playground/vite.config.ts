// Copyright (c) Meta Platforms, Inc. and affiliates.

import {createWebToolConfig} from '../vite.base.ts';
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {normalizePath, type ConfigEnv, type Plugin} from 'vite';

const BROWSER_RUNNER = normalizePath(fileURLToPath(new URL('./src/runBenchmark.ts', import.meta.url)));
const DESKTOP_RUNNER = normalizePath(fileURLToPath(new URL('./src/runBenchmark.desktop.ts', import.meta.url)));

// `--mode desktop` builds the renderer for Electron, which benchmarks with zli.
// Swapping out the browser runner drops the WASM worker from the bundle, along
// with the Emscripten output it imports, which a desktop build does not have.
const desktopRunner: Plugin = {
  name: 'desktop-runner',
  enforce: 'pre',
  async resolveId(source, importer, options) {
    if (!source.includes('runBenchmark')) {
      return null;
    }
    const resolved = await this.resolve(source, importer, {...options, skipSelf: true});
    return resolved?.id === BROWSER_RUNNER ? DESKTOP_RUNNER : resolved;
  },
};

// index.html carries the production CSP. The dev server additionally needs the
// inline React Refresh preamble from @vitejs/plugin-react and the HMR websocket.
const DEV_CSP_SOURCES: [directive: string, source: string][] = [
  ["script-src 'self'", "'unsafe-inline'"],
  ["connect-src 'self'", 'ws://localhost:*'],
];

const devContentSecurityPolicy: Plugin = {
  name: 'dev-content-security-policy',
  apply: 'serve',
  transformIndexHtml(html) {
    for (const [directive, source] of DEV_CSP_SOURCES) {
      if (!html.includes(directive)) {
        throw new Error(`index.html Content-Security-Policy is missing "${directive}"`);
      }
      html = html.replace(directive, `${directive} ${source}`);
    }
    return html;
  },
};

// Emscripten's output, which only a WASM build produces: the GitHub workflows
// run one before the docs, but the internal docs build does not. Without it the
// worker bundle cannot resolve `wasm_api.js`'s dynamic import of it and fails,
// so it is left for the browser to request, as the main bundle already leaves
// it -- and `wasm_api.js` says what is missing when that request fails.
const hasWasmArtifact = existsSync(fileURLToPath(new URL('../../wasm/js/openzl.js', import.meta.url)));

export default ({mode}: ConfigEnv) => ({
  ...createWebToolConfig({
    base: '/tools/playground',
    plugins: [devContentSecurityPolicy, ...(mode === 'desktop' ? [desktopRunner] : [])],
    testAlias: [
      {
        find: /^\/OpenZL_logo\.png/,
        replacement: fileURLToPath(new URL('./public/OpenZL_logo.png', import.meta.url)),
      },
    ],
    testSetupFiles: ['./tests/setup.ts'],
  }),
  // The benchmark worker is spawned with `{type: 'module'}`, so the build has
  // to emit it as one. The default IIFE output does not match and its imports
  // fail once built, though the dev server serves it either way.
  worker: {
    format: 'es' as const,
    ...(hasWasmArtifact ? {} : {rollupOptions: {external: [/\/openzl\.js$/]}}),
  },
  // Left unbundled so Vite's `new URL(..., import.meta.url)` transform runs
  // on it: esbuild's prebundling does not, and the wasm URL then points at a
  // file that is not there. The dev server answers those with the SPA
  // fallback, so it reads as a hang rather than a 404.
  optimizeDeps: {exclude: ['@bokuweb/zstd-wasm']},
  server: {
    // tools/wasm/js/ is a sibling of the Yarn workspace root, so the dev
    // server's default fs.allow refuses it and the wasm URL 403s.
    fs: {allow: ['../..']},
    // The OpenZL module is built with pthreads, which needs SharedArrayBuffer,
    // which browsers hand only to a cross-origin isolated document. The
    // deployed site cannot send these -- GitHub Pages serves fixed headers --
    // and gets isolation from a service worker instead. The dev server sends
    // them directly, which is also why it has to be reached over `localhost`:
    // isolation requires a secure context, and a bare hostname is not one.
    //
    // Worth knowing when the service worker lands: with these set, the dev
    // server cannot tell you whether it works.
    // The same values the service worker adds, so a cross-origin resource that
    // the deployed page would block is blocked here too.
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
  },
});
