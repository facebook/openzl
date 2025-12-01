// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InternalCodecNode} from './InternalCodecNode';
import {InternalGraphNode} from './InternalGraphNode';
import type {RF_nodeId} from './types';
import type {SerializedStreamdumpV1} from '../../interfaces/SerializedStreamdump';
import {InteractiveChunkGraph} from './InteractiveChunkGraph';
import type {InternalNode} from './InternalNode';
import type {InternalEdge} from './InternalEdge';

export class InteractiveStreamdumpGraph {
  private chunkGraphs: InteractiveChunkGraph[] = [];

  constructor(obj: SerializedStreamdumpV1, isDefaultCollapsed = false) {
    // Create a chunk graph for each chunk in the streamdump
    this.chunkGraphs = obj.chunks.map((chunk) => new InteractiveChunkGraph(chunk, isDefaultCollapsed));
  }

  // Trampoline methods that delegate to the first chunk graph
  // TODO: Support multi-chunk operations when the data format includes chunks

  getVisibleStreamdumpGraph(): {
    dagOrderedNodes: InternalNode[];
    edges: InternalEdge[];
  } {
    // TODO: Merge chunks into a single graph
    return this.chunkGraphs[0].getVisibleStreamdumpGraph();
  }

  toggleSubgraphCollapse(codec: InternalCodecNode): RF_nodeId[] {
    return this.chunkGraphs[0].toggleSubgraphCollapse(codec);
  }

  expandOneLevel(codec: InternalCodecNode): RF_nodeId[] {
    return this.chunkGraphs[0].expandOneLevel(codec);
  }

  toggleGraphCollapse(graph: InternalGraphNode): RF_nodeId[] {
    return this.chunkGraphs[0].toggleGraphCollapse(graph);
  }

  toggleGraphHide(graph: InternalGraphNode): RF_nodeId[] {
    return this.chunkGraphs[0].toggleGraphHide(graph);
  }

  toggleAllStandardGraphs(isCollapsed: boolean): void {
    this.chunkGraphs[0].toggleAllStandardGraphs(isCollapsed);
  }
}
