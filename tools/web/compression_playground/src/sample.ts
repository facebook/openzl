// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {BundledSample} from './benchmarkTypes.ts';
import {formatFileSize} from './fileSize.ts';
import sample from './bundledSample.json';

/**
 * The OpenZL example file the desktop app installs. `bundledSample.json` names
 * it for both the page and the main process; the page only needs its name and
 * size, to show it as the chosen file.
 */
export const BUNDLED_SAMPLE: BundledSample = {kind: 'bundled-sample', name: sample.name, size: sample.size};

export const TRY_SAMPLE_LABEL = `Try a ${formatFileSize(sample.size)} sample`;
