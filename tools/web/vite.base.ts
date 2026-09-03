// Copyright (c) Meta Platforms, Inc. and affiliates.

import {defineConfig} from 'vitest/config';
import react from '@vitejs/plugin-react';
import tsconfigPaths from 'vite-tsconfig-paths';

/**
 * Shared Vite config for OpenZL web tools.
 *
 * Each tool keeps only its `base` and any tool-specific `test.alias` overrides.
 * The common plugins (`@vitejs/plugin-react`, `vite-tsconfig-paths`) are
 * centralized here so a toolchain upgrade only touches one file.
 *
 * Usage in a tool (e.g. compression_playground/vite.config.ts):
 *   import {createWebToolConfig} from '../vite.base.ts';
 *   export default createWebToolConfig({base: '/tools/playground'});
 *
 *   // visualization_app with extra alias:
 *   export default createWebToolConfig({
 *     base: '/tools/trace',
 *     testAlias: [{find: /^\/OpenZL_logo\.png/, replacement: '...'}],
 *   });
 */
export function createWebToolConfig(options: {
  base: string;
  testAlias?: {find: RegExp; replacement: string}[];
}) {
  return defineConfig({
    base: options.base,
    plugins: [react(), tsconfigPaths()],
    ...(options.testAlias
      ? {
          test: {
            alias: options.testAlias,
          },
        }
      : {}),
  });
}
