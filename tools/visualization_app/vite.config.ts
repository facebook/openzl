// Copyright (c) Meta Platforms, Inc. and affiliates.

import {defineConfig} from 'vitest/config';
import react from '@vitejs/plugin-react';
import tsconfigPaths from 'vite-tsconfig-paths';
import {fileURLToPath} from 'node:url';

// https://vite.dev/config/
export default defineConfig({
  base: '/tools/trace',
  plugins: [react(), tsconfigPaths()],
  test: {
    // Toolbar imports the logo via the Vite public-dir path
    // '/OpenZL_logo.png?url', which the test runner can't resolve on its own.
    // Map it to the real file. The path is resolved relative to this config
    // (via import.meta.url) so it stays portable for open source.
    alias: [
      {
        find: /^\/OpenZL_logo\.png/,
        replacement: fileURLToPath(new URL('./public/OpenZL_logo.png', import.meta.url)),
      },
    ],
  },
});
