// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, it, expect} from 'vitest';
import {createSingleNodeGraph} from './utils/createTestModels';
import type {InteractiveChunkGraph} from '../src/graphVisualization/models/InteractiveChunkGraph';

describe('Test InteractiveChunkGraph internal APIs', () => {
  it('contains and findCodecByName work for present and missing', () => {
    const streamdumpGraph = createSingleNodeGraph();
    const chunkGraph = (streamdumpGraph as any).chunkGraphs[0] as InteractiveChunkGraph;

    const found = chunkGraph.findCodecByName('SingleNode');
    expect(found?.name).toBe('SingleNode');
    expect(chunkGraph.contains(found!)).toBe(true);

    const notFound = chunkGraph.findCodecByName('Missing');
    expect(notFound).toBeNull();
  });
});
