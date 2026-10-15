// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import fs from 'node:fs';
import {describe, expect, it} from 'vitest';
import packageJson from '../package.json';
import {SAMPLE_NAME, SAMPLE_SIZE, SAMPLE_SOURCE_PATH} from '../electron/sample.cjs';

describe('the bundled sample', () => {
  it('is the file the shared constants name, at the size they give', () => {
    // The page shows the name and size without reading the file, so they must not drift.
    expect(fs.statSync(SAMPLE_SOURCE_PATH).size).toBe(SAMPLE_SIZE);
  });

  it('is installed under the name the main process looks for', () => {
    const resource = packageJson.build.extraResources.find((entry) => entry.to === SAMPLE_NAME);
    expect(resource).toBeDefined();
    expect(fs.realpathSync(new URL(`../${resource?.from}`, import.meta.url))).toBe(fs.realpathSync(SAMPLE_SOURCE_PATH));
  });
});
