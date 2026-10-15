// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {app, BrowserWindow, dialog, ipcMain, Menu, net, protocol, session, shell, utilityProcess} = require('electron');
const {APP_ORIGIN, APP_SCHEME, originOf, resolveBundlePath} = require('./app-protocol.cjs');
const {validateRequest} = require('./benchmark-request.cjs');
const {isAllowedExternalUrl} = require('./external-links.cjs');

const BENCHMARK_CHANNEL = 'openzl:benchmark';

let mainWindow;
let rendererOrigin;

// Must run before the app is ready. A standard, secure scheme gives the bundle a
// real origin, so CSP 'self', fetch, and module scripts behave as on the web.
protocol.registerSchemesAsPrivileged([
  {scheme: APP_SCHEME, privileges: {standard: true, secure: true, supportFetchAPI: true}},
]);

function zliPath() {
  const executable = process.platform === 'win32' ? 'zli.exe' : 'zli';
  return app.isPackaged
    ? path.join(process.resourcesPath, 'native', executable)
    : path.join(__dirname, 'native', 'build', 'Release', executable);
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

function runBenchmark(request) {
  return new Promise((resolve, reject) => {
    const worker = utilityProcess.fork(nativeHostPath(), [], {
      serviceName: 'OpenZL benchmark',
      stdio: 'inherit',
    });
    let settled = false;
    worker.once('message', (message) => {
      settled = true;
      worker.kill();
      if (message.ok) {
        resolve(message.results);
      } else {
        reject(new Error(message.error));
      }
    });
    worker.once('error', (type, location) => {
      settled = true;
      worker.kill();
      reject(new Error(`Native benchmark process failed (${type} at ${location})`));
    });
    worker.once('exit', (code) => {
      if (!settled) {
        reject(new Error(`Native benchmark process exited unexpectedly (code ${code})`));
      }
    });
    worker.once('spawn', () => {
      try {
        worker.postMessage({zliPath: zliPath(), request});
      } catch (error) {
        settled = true;
        worker.kill();
        reject(error);
      }
    });
  });
}

// The UI needs no browser permissions, and Electron grants requests by default.
function denyPermissions() {
  session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  session.defaultSession.setDevicePermissionHandler(() => false);
}

function registerIpc() {
  ipcMain.handle(BENCHMARK_CHANNEL, (event, rawRequest) => {
    if (!isTrustedSender(event)) {
      throw new Error('Untrusted benchmark request');
    }
    return runBenchmark(validateRequest(rawRequest));
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
  Menu.setApplicationMenu(null);
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

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
