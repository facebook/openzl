// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const {contextBridge, ipcRenderer, webUtils} = require('electron');

// Each run gets its own port, so the page never handles IPC or ports directly.
// `onEvent` receives each `step`, `result` and `failure` until the run ends;
// `done` then settles, rejecting if the run failed or was cancelled.
function startBenchmark(request, onEvent) {
  const {port1, port2} = new MessageChannel();
  let settled = false;
  const done = new Promise((resolve, reject) => {
    port1.onmessage = ({data}) => {
      if (data?.type === 'step' || data?.type === 'result' || data?.type === 'failure') {
        onEvent?.(data);
        return;
      }
      settled = true;
      port1.close();
      if (data?.type === 'finished') {
        resolve();
      } else {
        reject(new Error(data?.type === 'cancelled' ? 'Benchmark cancelled' : (data?.message ?? 'Benchmark failed')));
      }
    };
  });
  ipcRenderer.postMessage('openzl:benchmark', request, [port2]);
  // Cancelling a run that has ended does nothing, rather than posting to a closed port.
  const cancel = () => {
    if (!settled) {
      port1.postMessage({type: 'cancel'});
    }
  };
  return Object.freeze({done, cancel});
}

contextBridge.exposeInMainWorld(
  'openzlDesktop',
  Object.freeze({
    startBenchmark,
    getPathForFile: (file) => webUtils.getPathForFile(file),
  }),
);
