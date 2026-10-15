// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {app, BrowserWindow, dialog, ipcMain, Menu, net, protocol, session, shell, utilityProcess} = require('electron');
const {menuTemplate} = require('./app-menu.cjs');
const {APP_ORIGIN, APP_SCHEME, originOf, resolveBundlePath} = require('./app-protocol.cjs');
const {validateRequest} = require('./benchmark-request.cjs');
const {SAMPLE_NAME, SAMPLE_SOURCE_PATH} = require('./sample.cjs');
const {parseWorkerMessage} = require('./worker-messages.cjs');
const {isAllowedExternalUrl} = require('./external-links.cjs');

const BENCHMARK_CHANNEL = 'openzl:benchmark';
const CANCEL_GRACE_MS = 5000;

let mainWindow;
let rendererOrigin;
const activeRuns = new Set();

// Must run before the app is ready. A standard, secure scheme gives the bundle a
// real origin, so CSP 'self', fetch, and module scripts behave as on the web.
protocol.registerSchemesAsPrivileged([
  {scheme: APP_SCHEME, privileges: {standard: true, secure: true, supportFetchAPI: true}},
]);

function nativeBinaryPath(name) {
  const executable = process.platform === 'win32' ? `${name}.exe` : name;
  return app.isPackaged
    ? path.join(process.resourcesPath, 'native', executable)
    : path.join(__dirname, 'native', 'build', 'Release', executable);
}

function samplePath() {
  return app.isPackaged ? path.join(process.resourcesPath, SAMPLE_NAME) : SAMPLE_SOURCE_PATH;
}

/** The request the worker runs: the bundled sample, if asked for, as the path it is installed at. */
function resolveInput(request) {
  if (!('sample' in request.input)) {
    return request;
  }
  const sample = samplePath();
  if (!fs.existsSync(sample)) {
    throw new Error(
      app.isPackaged ? 'The bundled sample is missing from this installation' : `The sample is missing: ${sample}`,
    );
  }
  return {...request, input: {path: sample}};
}

function nativeHostPath() {
  return app.isPackaged
    ? path.join(process.resourcesPath, 'native', 'native-worker.cjs')
    : path.join(__dirname, 'native-worker.cjs');
}

function isTrustedSender(event) {
  if (
    mainWindow === undefined ||
    event.sender !== mainWindow.webContents ||
    event.senderFrame !== event.sender.mainFrame
  ) {
    return false;
  }
  // Both sides can be undefined, and undefined === undefined must not pass.
  const origin = originOf(event.senderFrame.url);
  return origin !== undefined && origin === rendererOrigin;
}

function serveRenderer() {
  const root = path.join(__dirname, '..', 'dist');
  protocol.handle(APP_SCHEME, (request) => {
    const filePath = resolveBundlePath(root, request.url);
    // A path inside the bundle can still name a file that is not there.
    return filePath === undefined
      ? new Response('Not found', {status: 404})
      : net.fetch(pathToFileURL(filePath).toString()).catch(() => new Response('Not found', {status: 404}));
  });
}

/**
 * One benchmark run in its own utility process, reporting to the renderer over
 * `port`. The main process records every native PID the worker reports, so it can
 * kill them if the worker dies or ignores a cancel: children do not exit with
 * their parent on their own. It also owns the run's scratch directory, which a
 * killed worker could not clean up.
 */
function startRun(request, port) {
  // Before the fork: a worker started first would be leaked if this threw, since
  // nothing has recorded it yet for `will-quit` to clean up.
  let tempRoot;
  try {
    tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'openzl-run-'));
  } catch (error) {
    port.postMessage({type: 'error', message: `Could not create a scratch directory: ${error.message}`});
    port.close();
    return;
  }
  const worker = utilityProcess.fork(nativeHostPath(), [], {
    serviceName: 'OpenZL benchmark',
    stdio: 'inherit',
  });
  const run = {childPids: new Set(), tempRoot, worker, port, finished: false, watchdog: undefined};
  run.cancel = () => {
    if (!run.finished && run.watchdog === undefined) {
      worker.postMessage({type: 'cancel'});
      run.watchdog = setTimeout(() => disposeRun(run, {type: 'cancelled'}), CANCEL_GRACE_MS);
    }
  };

  worker.on('message', (rawMessage) => {
    let message;
    try {
      message = parseWorkerMessage(rawMessage);
    } catch (error) {
      disposeRun(run, {type: 'error', message: error.message});
      return;
    }
    if (message.type === 'child') {
      if (message.state === 'exited') {
        run.childPids.delete(message.pid);
      } else if (run.finished) {
        // Reported after the run's children were killed, so nothing else will.
        killPid(message.pid);
      } else {
        run.childPids.add(message.pid);
      }
    } else if (message.type === 'progress' || message.type === 'partial') {
      if (!run.finished) {
        port.postMessage(message);
      }
    } else if (['results', 'error', 'cancelled'].includes(message.type)) {
      disposeRun(run, message);
    }
  });
  worker.once('error', (type, location) =>
    disposeRun(run, {type: 'error', message: `Native benchmark process failed (${type} at ${location})`}),
  );
  // A worker that exits while cancelling may not have reported it first.
  worker.once('exit', (code) =>
    disposeRun(
      run,
      run.watchdog === undefined
        ? {type: 'error', message: `Native benchmark process exited unexpectedly (code ${code})`}
        : {type: 'cancelled'},
    ),
  );
  worker.once('spawn', () => {
    try {
      worker.postMessage({
        type: 'start',
        zliPath: nativeBinaryPath('zli'),
        zstdBenchmarkPath: nativeBinaryPath('zstd-benchmark'),
        request,
        tempRoot: run.tempRoot,
      });
    } catch (error) {
      // The worker died between spawning and receiving its start message.
      disposeRun(run, {type: 'error', message: `Could not start the benchmark process: ${error.message}`});
    }
  });

  port.on('message', ({data}) => {
    if (data?.type === 'cancel') {
      run.cancel();
    }
  });
  // The renderer closed its end: it reloaded, crashed, or its window closed.
  port.on('close', () => run.cancel());
  port.start();
  activeRuns.add(run);
}

/**
 * Ends `run` and sends the renderer `message`, once: later calls do nothing.
 * Kills the run's zli processes and its worker, and removes its scratch
 * directory.
 */
function disposeRun(run, message) {
  if (run.finished) {
    return;
  }
  run.finished = true;
  clearTimeout(run.watchdog);
  activeRuns.delete(run);
  killChildren(run);
  // Before the directory goes: a worker still running could be writing into it.
  run.worker.kill();
  removeTempRoot(run);
  run.port.postMessage(message);
  run.port.close();
}

function killPid(pid) {
  try {
    process.kill(pid, 'SIGKILL');
  } catch {
    // Already exited.
  }
}

function killChildren(run) {
  for (const pid of run.childPids) {
    killPid(pid);
  }
  run.childPids.clear();
}

function removeTempRoot(run) {
  try {
    // The processes just killed may not have released their files yet, which
    // blocks removal on Windows; the retries wait them out.
    fs.rmSync(run.tempRoot, {recursive: true, force: true, maxRetries: 3});
  } catch (error) {
    // Cleanup must not stop the run from reporting its outcome.
    console.warn(`Could not remove ${run.tempRoot}: ${error.message}`);
  }
}

// The UI needs no browser permissions, and Electron grants requests by default.
function denyPermissions() {
  session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  session.defaultSession.setDevicePermissionHandler(() => false);
}

function registerIpc() {
  ipcMain.on(BENCHMARK_CHANNEL, (event, rawRequest) => {
    const [port] = event.ports;
    if (port === undefined) {
      return;
    }
    let request;
    try {
      if (!isTrustedSender(event)) {
        throw new Error('Untrusted benchmark request');
      }
      // Concurrent runs would compete for CPU and distort each other's timings.
      if (activeRuns.size > 0) {
        throw new Error('A benchmark is already running');
      }
      request = resolveInput(validateRequest(rawRequest));
    } catch (error) {
      port.postMessage({type: 'error', message: error.message});
      port.close();
      return;
    }
    startRun(request, port);
  });
}

async function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1320,
    height: 900,
    minWidth: 900,
    minHeight: 640,
    title: 'OpenZL Compression Playground',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      preload: path.join(__dirname, 'preload.cjs'),
    },
  });

  mainWindow.webContents.setWindowOpenHandler(({url}) => {
    if (isAllowedExternalUrl(url)) {
      void shell.openExternal(url);
    }
    return {action: 'deny'};
  });
  mainWindow.webContents.on('will-navigate', (event, url) => {
    event.preventDefault();
    if (isAllowedExternalUrl(url)) {
      void shell.openExternal(url);
    }
  });

  const devServer = process.argv
    .find((argument) => argument.startsWith('--dev-server='))
    ?.slice('--dev-server='.length);
  if (!app.isPackaged && devServer !== undefined) {
    rendererOrigin = originOf(devServer);
    await mainWindow.loadURL(devServer);
    if (process.argv.includes('--devtools')) {
      mainWindow.webContents.openDevTools({mode: 'detach'});
    }
  } else {
    rendererOrigin = APP_ORIGIN;
    await mainWindow.loadURL(`${APP_ORIGIN}/index.html`);
  }
}

const started = app.whenReady().then(async () => {
  Menu.setApplicationMenu(
    Menu.buildFromTemplate(
      menuTemplate({
        platform: process.platform,
        isPackaged: app.isPackaged,
        openExternal: (url) => void shell.openExternal(url),
      }),
    ),
  );
  denyPermissions();
  serveRenderer();
  registerIpc();
  await createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      void createWindow();
    }
  });
});

// Otherwise a failed start is a blank window, or no window and a process left
// running, with the reason only in a console no one is watching.
started.catch((error) => {
  dialog.showErrorBox(
    'OpenZL Compression Playground could not start',
    error instanceof Error ? error.message : String(error),
  );
  app.exit(1);
});

app.on('will-quit', () => {
  for (const run of activeRuns) {
    disposeRun(run, {type: 'cancelled'});
  }
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
