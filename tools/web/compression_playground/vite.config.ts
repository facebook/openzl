// Copyright (c) Meta Platforms, Inc. and affiliates.

import {createWebToolConfig} from '../vite.base.ts';
import {fileURLToPath} from 'node:url';

const config = createWebToolConfig({
  base: '/tools/playground',
  testAlias: [
    {
      find: /^\/OpenZL_logo\.png/,
      replacement: fileURLToPath(new URL('./public/OpenZL_logo.png', import.meta.url)),
    },
  ],
  testSetupFiles: ['./tests/setup.ts'],
});

export default {
  ...config,
  // The benchmark worker is spawned with `{type: 'module'}`, so the build has
  // to emit it as one. The default IIFE output does not match and its imports
  // fail once built, though the dev server serves it either way.
  worker: {format: 'es' as const},
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
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'credentialless',
    },
  },
};
