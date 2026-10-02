// Copyright (c) Meta Platforms, Inc. and affiliates.

import {afterEach, describe, expect, it, vi} from 'vitest';
import type {RunConfig, WorkerMessage} from '../src/benchmarkTypes.ts';

const measureRun = vi.hoisted(() => vi.fn());
vi.mock('../src/measureRun.ts', () => ({measureRun}));

/**
 * The entry registers its listener at module scope, so `self` is stubbed and
 * the module re-imported per case. Nothing here starts a real worker: that the
 * file loads as one is a browser property, not something vitest can assert.
 */
async function loadWorker() {
  vi.resetModules();
  measureRun.mockReset();
  const posted: WorkerMessage[] = [];
  let onMessage: ((event: MessageEvent) => void) | undefined;
  vi.stubGlobal('self', {
    addEventListener: (type: string, handler: (event: MessageEvent) => void) => {
      if (type === 'message') {
        onMessage = handler;
      }
    },
    postMessage: (message: WorkerMessage) => posted.push(message),
  });
  await import('../src/benchmarkWorker.ts');
  return {
    posted,
    send: (config: RunConfig) => {
      onMessage?.({data: {config}} as MessageEvent);
    },
  };
}

afterEach(() => {
  vi.unstubAllGlobals();
});

const config = {iterations: 3} as RunConfig;

describe('benchmarkWorker', () => {
  it('measures the config it was handed', async () => {
    const {send} = await loadWorker();
    measureRun.mockResolvedValue(undefined);

    send(config);

    expect(measureRun.mock.calls[0][0]).toBe(config);
  });

  it('forwards every message the run emits, in order', async () => {
    // The page reconstructs the whole run from these, so one dropped or
    // reordered message is a run that never finishes.
    const {posted, send} = await loadWorker();
    measureRun.mockImplementation(async (_config: RunConfig, post: (m: WorkerMessage) => void) => {
      post({type: 'loading'});
      post({type: 'finished'});
    });

    send(config);
    await vi.waitFor(() => {
      expect(posted).toHaveLength(2);
    });

    expect(posted.map((message) => message.type)).toEqual(['loading', 'finished']);
  });

  it('turns a rejected run into a failed message', async () => {
    // Without the catch this is an unhandled rejection inside a worker: the
    // page is told nothing and sits on `running` for good.
    const {posted, send} = await loadWorker();
    measureRun.mockRejectedValue(new Error('the module could not start'));

    send(config);
    await vi.waitFor(() => {
      expect(posted).toHaveLength(1);
    });

    expect(posted[0]).toEqual({type: 'failed', message: 'the module could not start'});
  });
});
