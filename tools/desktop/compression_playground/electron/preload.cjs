// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const {contextBridge, ipcRenderer, webUtils} = require('electron');

contextBridge.exposeInMainWorld(
  'openzlDesktop',
  Object.freeze({
    benchmark: (request) => ipcRenderer.invoke('openzl:benchmark', request),
    getPathForFile: (file) => webUtils.getPathForFile(file),
  }),
);
