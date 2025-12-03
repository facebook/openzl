// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InternalCodecNode} from './InternalCodecNode';
import {InternalGraphNode} from './InternalGraphNode';
import type {RF_nodeId} from './types';
import type {SerializedStreamdumpV1} from '../../interfaces/SerializedStreamdump';
import {InteractiveChunkGraph} from './InteractiveChunkGraph';
import type {InternalNode} from './InternalNode';
import {InternalEdge} from './InternalEdge';
import {InsertOnlyJournal} from '../../utils/InsertOnlyJournal';
import {InternalSegmenterNode} from './InternalSegmenterNode';

export class InteractiveStreamdumpGraph {
  private chunkGraphs: InteractiveChunkGraph[] = [];
  private visibleChunkIndex: number | null = 0;

  constructor(obj: SerializedStreamdumpV1, isDefaultCollapsed = false) {
    // Create a chunk graph for each chunk in the streamdump
    this.chunkGraphs = obj.chunks.map((chunk) => new InteractiveChunkGraph(chunk, isDefaultCollapsed));
    const maybeSegmenterNode = this.chunkGraphs[0].findCodecByName('segmenter');
    if (maybeSegmenterNode) {
      (maybeSegmenterNode as InternalSegmenterNode).numChunks = this.chunkGraphs.length - 1;
    }
  }

  // Set which chunk should be visible alongside the 0th chunk
  // Pass null to only display the 0th chunk
  setVisibleChunk(chunkIndex: number | null): void {
    if (chunkIndex !== null && (chunkIndex < 0 || chunkIndex >= this.chunkGraphs.length)) {
      throw new Error(`Invalid chunk index: ${chunkIndex}. Must be between 0 and ${this.chunkGraphs.length - 1}`);
    }
    this.visibleChunkIndex = chunkIndex;
  }

  getVisibleStreamdumpGraph(): {
    dagOrderedNodes: InternalNode[];
    edges: InternalEdge[];
  } {
    // If no additional chunk is selected, just return the 0th chunk
    if (this.visibleChunkIndex === null || this.visibleChunkIndex === 0) {
      return this.chunkGraphs[0].getVisibleStreamdumpGraph();
    }

    // Merge the 0th chunk with the selected chunk
    const chunk0 = this.chunkGraphs[0].getVisibleStreamdumpGraph();
    const chunkN = this.chunkGraphs[this.visibleChunkIndex].getVisibleStreamdumpGraph();

    const segmenterCodec = this.chunkGraphs[0].findCodecByName('segmenter') as InternalSegmenterNode;
    // todo: no need for this hack if we set it correctly
    segmenterCodec.numChunks = this.chunkGraphs.length - 1;

    const zlStartCodec = this.chunkGraphs[this.visibleChunkIndex].findCodecByName('zl.#start');
    if (!segmenterCodec) {
      console.warn('Could not find segmenter codec in chunk 0');
      return chunk0;
    }
    if (!zlStartCodec) {
      console.warn(`Could not find zl.#start codec in chunk ${this.visibleChunkIndex}`);
      return chunk0;
    }

    // Merge nodes
    const mergedNodeSet = new InsertOnlyJournal<InternalNode>();
    chunk0.dagOrderedNodes.forEach((node) => mergedNodeSet.insert(node));
    chunkN.dagOrderedNodes.forEach((node) => {
      if (node !== zlStartCodec) {
        mergedNodeSet.insert(node);
      }
    });

    // Merge edges
    const mergedEdgeSet = new InsertOnlyJournal<InternalEdge>();
    chunk0.edges.forEach((edge) => mergedEdgeSet.insert(edge));
    // Replace connecting edges from segmenter to the first node in chunk N
    const firstStreams = zlStartCodec.outputStreams;
    for (const streamNum of firstStreams) {
      const stream = chunkN.edges.find((edge) => edge.streamId === streamNum);
      if (stream == null) {
        const msg = `Could not find stream ${streamNum} in chunk ${this.visibleChunkIndex}. This is not supposed to happen :(`;
        console.assert(stream != null, msg);
        continue;
      }
      const connectingEdge = InternalEdge.constructFromInternalEdge(stream.rfid, segmenterCodec, stream.target, stream);
      mergedEdgeSet.insert(connectingEdge);
    }
    chunkN.edges.forEach((edge) => {
      for (const streamNum of firstStreams) {
        if (edge.streamId === streamNum) {
          return;
        }
      }
      mergedEdgeSet.insert(edge);
    });

    return {
      dagOrderedNodes: Array.from(mergedNodeSet),
      edges: Array.from(mergedEdgeSet),
    };
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
