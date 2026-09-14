// Copyright (c) Meta Platforms, Inc. and affiliates.

import {createWebToolConfig} from '../vite.base.ts';
import {fileURLToPath} from 'node:url';

export default createWebToolConfig({
  base: '/tools/trace',
  testAlias: [
    {
      find: /^\/OpenZL_logo\.png/,
      replacement: fileURLToPath(new URL('./public/OpenZL_logo.png', import.meta.url)),
    },
  ],
});
