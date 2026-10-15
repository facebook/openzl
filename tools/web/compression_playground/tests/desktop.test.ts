// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {describe, expect, it, vi} from 'vitest';
import {inputForFile, type OpenZLDesktopBridge} from '../src/desktop.ts';

function bridgeWithPath(path: string): OpenZLDesktopBridge {
  return {getPathForFile: () => path, startBenchmark: vi.fn()};
}

describe('desktop input', () => {
  it('passes a path through when the file has one', async () => {
    await expect(inputForFile(new File(['x'], 'a.bin'), bridgeWithPath('/tmp/a.bin'))).resolves.toEqual({
      path: '/tmp/a.bin',
    });
  });

  it('reads the bytes when it has none', async () => {
    await expect(inputForFile(new File(['abc'], 'a.bin'), bridgeWithPath(''))).resolves.toEqual({
      data: new TextEncoder().encode('abc'),
    });
  });

  it('takes a path for a file of any size, without reading it', async () => {
    const file = {size: 10 * 1024 ** 3} as unknown as File;

    await expect(inputForFile(file, bridgeWithPath('/tmp/big.bin'))).resolves.toEqual({path: '/tmp/big.bin'});
  });
});
