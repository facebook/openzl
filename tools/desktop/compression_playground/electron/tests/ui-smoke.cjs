// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

// Run with Electron after `desktop:build:renderer` and `desktop:build:native`:
// drives the real page the way a user would. It turns training off, clicks the
// sample button, and waits for the run to fill in the results table and the
// charts, so the whole path from the page through the backend to `zli` and
// back is exercised together.

const assert = require('node:assert/strict');
const {app, BrowserWindow} = require('electron');

require('../main.cjs');

const TIMEOUT_MS = 5 * 60_000;

async function mainWindow() {
  const deadline = Date.now() + TIMEOUT_MS;
  for (;;) {
    const [window] = BrowserWindow.getAllWindows();
    if (window !== undefined) {
      if (window.webContents.isLoading()) {
        await new Promise((resolve) => window.webContents.once('did-finish-load', resolve));
      }
      return window;
    }
    assert.ok(Date.now() < deadline, 'The app window never appeared');
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

async function waitFor(window, expression, what) {
  const deadline = Date.now() + TIMEOUT_MS;
  for (;;) {
    const value = await window.webContents.executeJavaScript(expression);
    if (value) {
      return value;
    }
    assert.ok(Date.now() < deadline, `Timed out waiting for ${what}`);
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
}

async function main() {
  const window = await mainWindow();
  const page = (expression) => window.webContents.executeJavaScript(expression);

  await waitFor(
    window,
    `[...document.querySelectorAll('button')].some((button) => /^Try a .* sample$/.test(button.textContent.trim()))`,
    'the sample button',
  );

  // Training the sample takes minutes; native-training-smoke.cjs covers it.
  const trainCheckbox = `[...document.querySelectorAll('label')]
    .find((element) => element.textContent.trim() === 'TRAIN')
    ?.querySelector('input[type=checkbox]')`;
  const found = await page(`(() => {
    const checkbox = ${trainCheckbox};
    if (checkbox === undefined || checkbox === null) {
      return false;
    }
    if (checkbox.checked) {
      checkbox.click();
    }
    return true;
  })()`);
  assert.ok(found, 'No TRAIN checkbox');
  // The checkbox is controlled, so its state lands once the page re-renders.
  await waitFor(window, `${trainCheckbox}?.checked === false`, 'training to turn off');

  await page(`[...document.querySelectorAll('button')]
    .find((button) => /^Try a .* sample$/.test(button.textContent.trim()))
    .click()`);

  const done = await waitFor(
    window,
    `(() => {
      const match = /Done — (.+) · (\\d+) succeeded, (\\d+) failed/.exec(document.body.innerText);
      return match === null ? null : {size: match[1], succeeded: Number(match[2]), failed: Number(match[3])};
    })()`,
    'the run to finish',
  );
  // The charts measure their container before drawing, so they can fill in
  // after the table; give them a moment rather than reading them once.
  let results;
  const chartsDeadline = Date.now() + 30_000;
  for (;;) {
    results = await page(`({
      chosen: document.querySelector('[data-testid="upload-dropzone"]')?.innerText ?? '',
      tableRows: document.querySelectorAll('table[aria-labelledby="measurements-title"] tbody tr').length,
      chartPoints: document.querySelectorAll('.recharts-scatter-symbol').length,
    })`);
    if ((results.tableRows >= 3 && results.chartPoints >= results.tableRows) || Date.now() > chartsDeadline) {
      break;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }

  // One row each for OpenZL, zstd and gzip, the page's defaults.
  assert.deepEqual({succeeded: done.succeeded, failed: done.failed}, {succeeded: 3, failed: 0});
  assert.match(results.chosen, /era5_ints\.bin/);
  assert.ok(results.tableRows >= 3, `expected a table row per measurement, got ${results.tableRows}`);
  assert.ok(
    results.chartPoints >= results.tableRows,
    `expected the charts to plot each of ${results.tableRows} measurements, got ${results.chartPoints} points`,
  );

  process.stdout.write(`${JSON.stringify({ok: true, ...done, ...results}, null, 2)}\n`);
}

app
  .whenReady()
  .then(main)
  .then(
    () => app.exit(0),
    (error) => {
      process.stderr.write(`${error instanceof Error ? error.stack : String(error)}\n`);
      app.exit(1);
    },
  );
