// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {describe, expect, it} from 'vitest';

const ROOT = fileURLToPath(new URL('..', import.meta.url));

interface ExtraResource {
  from: string;
  to: string;
}

const {extraResources} = (
  JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')) as {build: {extraResources: ExtraResource[]}}
).build;

function requiresOf(file: string): string[] {
  return [...fs.readFileSync(path.join(ROOT, file), 'utf8').matchAll(/require\('([^']+)'\)/g)].map((match) => match[1]);
}

/** The worker and every file it loads, relative to the package. */
function workerFiles(): string[] {
  const files = new Set<string>();
  const pending = ['electron/native-worker.cjs'];
  for (let file = pending.pop(); file !== undefined; file = pending.pop()) {
    if (files.has(file)) {
      continue;
    }
    files.add(file);
    for (const specifier of requiresOf(file)) {
      if (specifier.startsWith('.')) {
        pending.push(path.posix.join(path.posix.dirname(file), specifier));
      }
    }
  }
  return [...files];
}

// The worker runs from resources/native, outside the asar, so nothing reaches
// it unless electron-builder copies it there by name.
describe('the packaged benchmark worker', () => {
  it('ships every file the worker loads beside it', () => {
    const packaged = new Map(extraResources.map((resource) => [resource.to, resource.from]));
    for (const file of workerFiles()) {
      expect(packaged.get(`native/${path.posix.basename(file)}`), file).toBe(file);
    }
  });

  it('loads no package but Node built-ins and zod, which after-pack.cjs copies', () => {
    const packages = workerFiles()
      .flatMap(requiresOf)
      .filter((specifier) => !specifier.startsWith('.') && !specifier.startsWith('node:'));
    expect(new Set(packages)).toEqual(new Set(['zod']));
  });
});
