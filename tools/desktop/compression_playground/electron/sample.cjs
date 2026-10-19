// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const path = require('node:path');
// Shared with the page, which shows the sample's name and size without reading it.
const {webConstants} = require('./web-constants.cjs');
const sample = webConstants('bundledSample.json');

const SAMPLE_NAME = sample.name;
const SAMPLE_SIZE = sample.size;

/**
 * The sample in the source tree, which electron-builder copies into the app's
 * resources. `sample.path` is relative to the OpenZL repository root.
 */
const SAMPLE_SOURCE_PATH = path.resolve(__dirname, '..', '..', '..', '..', sample.path);

module.exports = {SAMPLE_NAME, SAMPLE_SIZE, SAMPLE_SOURCE_PATH};
