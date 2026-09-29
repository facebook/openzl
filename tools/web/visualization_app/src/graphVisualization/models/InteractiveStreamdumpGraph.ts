// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InternalCodecNode} from './InternalCodecNode';
import {InternalGraphNode} from './InternalGraphNode';
import {NodeType, type RF_nodeId} from './types';
import type {SerializedStreamdumpV1} from '../../interfaces/SerializedStreamdump';
import {InteractiveChunkGraph} from './InteractiveChunkGraph';
import type {InternalNode} from './InternalNode';
import {InternalEdge} from './InternalEdge';
import {InsertOnlyJournal} from '../../utils/InsertOnlyJournal';
import {InternalSegmenterNode} from './InternalSegmenterNode';
import {OperationType} from '../../models/idTypes';

export class InteractiveStreamdumpGraph {
  private chunkGraphs: InteractiveChunkGraph[] = [];
  private visibleChunkIndex: number | null = 0;
  private operationType: OperationType;

  constructor(obj: SerializedStreamdumpV1, isDefaultCollapsed = false) {
    // Create a chunk graph for each chunk in the streamdump
    this.operationType = obj.operationType === 1 ? OperationType.Decompress : OperationType.Compress;
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
    if (!segmenterCodec) {
      console.warn('Could not find segmenter codec in chunk 0');
      return chunk0;
    }
    // todo: no need for this hack if we set it correctly
    segmenterCodec.numChunks = this.chunkGraphs.length - 1;

    const startCodecName = this.operationType === OperationType.Compress ? 'zl.#start' : 'zl.#regen';
    const zlStartCodec = this.chunkGraphs[this.visibleChunkIndex].findCodecByName(startCodecName);
    if (!zlStartCodec) {
      console.warn(`Could not find ${startCodecName} codec in chunk ${this.visibleChunkIndex}`);
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

  getCodecChildren(codec: InternalCodecNode): InternalCodecNode[] {
    if (this.chunkGraphs[0].contains(codec)) {
      return this.chunkGraphs[0].getCodecChildren(codec);
    }
    if (this.visibleChunkIndex != null && this.chunkGraphs[this.visibleChunkIndex].contains(codec)) {
      return this.chunkGraphs[this.visibleChunkIndex].getCodecChildren(codec);
    }
    return [];
  }

  toggleSubgraphCollapse(codec: InternalCodecNode): {newlyVisibleNodes: RF_nodeId[], rebuiltNavlinkNodes: InternalNode[]} {
    if (this.chunkGraphs[0].contains(codec)) {
      return this.chunkGraphs[0].toggleSubgraphCollapse(codec);
    }
    if (this.visibleChunkIndex != null && this.chunkGraphs[this.visibleChunkIndex].contains(codec)) {
      return this.chunkGraphs[this.visibleChunkIndex].toggleSubgraphCollapse(codec);
    }
    throw new Error(
      `Could not find codec ${codec.id} in root chunk or currently selected chunk ${this.visibleChunkIndex}`,
    );
  }

  expandOneLevel(codec: InternalCodecNode): {newlyVisibleNodes: RF_nodeId[], rebuiltNavlinkNodes: InternalNode[]} {
    if (this.chunkGraphs[0].contains(codec)) {
      return this.chunkGraphs[0].expandOneLevel(codec);
    }
    if (this.visibleChunkIndex != null && this.chunkGraphs[this.visibleChunkIndex].contains(codec)) {
      return this.chunkGraphs[this.visibleChunkIndex].expandOneLevel(codec);
    }
    throw new Error(
      `Could not find codec ${codec.id} in root chunk or currently selected chunk ${this.visibleChunkIndex}`,
    );
  }

  toggleGraphCollapse(graph: InternalGraphNode): {newlyVisibleNodes: RF_nodeId[], rebuiltNavlinkNodes: InternalNode[]} {
    if (this.chunkGraphs[0].contains(graph)) {
      return this.chunkGraphs[0].toggleGraphCollapse(graph);
    }
    if (this.visibleChunkIndex != null && this.chunkGraphs[this.visibleChunkIndex].contains(graph)) {
      return this.chunkGraphs[this.visibleChunkIndex].toggleGraphCollapse(graph);
    }
    throw new Error(
      `Could not find graph ${graph.id} in root chunk or currently selected chunk ${this.visibleChunkIndex}`,
    );
  }

  toggleGraphHide(graph: InternalGraphNode): {newlyVisibleNodes: RF_nodeId[], rebuiltNavlinkNodes: InternalNode[]} {
    if (this.chunkGraphs[0].contains(graph)) {
      return this.chunkGraphs[0].toggleGraphHide(graph);
    }
    if (this.visibleChunkIndex != null && this.chunkGraphs[this.visibleChunkIndex].contains(graph)) {
      return this.chunkGraphs[this.visibleChunkIndex].toggleGraphHide(graph);
    }
    throw new Error(
      `Could not find graph ${graph.id} in root chunk or currently selected chunk ${this.visibleChunkIndex}`,
    );
  }

  toggleAllStandardGraphs(isCollapsed: boolean): void {
    for (const chunkGraph of this.chunkGraphs) {
      chunkGraph.toggleAllStandardGraphs(isCollapsed);
    }
  }

  buildAllNavlinks(): void {
    const {dagOrderedNodes, edges} = this.getVisibleStreamdumpGraph();

    for (const node of dagOrderedNodes) {
      node.parents = [];
      node.children = [];
    }

    // Only link nodes that are in the visible set (avoids dangling refs to omitted chunk start nodes)
    const visibleRfids = new Set(dagOrderedNodes.map((n) => n.rfid));
    const navigableTypes = new Set([NodeType.Codec, NodeType.Graph, NodeType.Segmenter]);
    for (const edge of edges) {
      const {source, target} = edge;
      if (!navigableTypes.has(source.type) || !navigableTypes.has(target.type)) continue;
      if (!visibleRfids.has(source.rfid) || !visibleRfids.has(target.rfid)) continue;
      if (source instanceof InternalGraphNode && !source.isCollapsed) continue;
      if (target instanceof InternalGraphNode && !target.isCollapsed) continue;
      if (!target.parents.includes(source.rfid)) {
        target.parents.push(source.rfid);
      }
      if (!source.children.includes(target.rfid)) {
        source.children.push(target.rfid);
      }
    }

    // Build a map from (source rfid, target rfid) to edge share for sorting
    const edgeShareMap = new Map<string, number>();
    for (const edge of edges) {
      const key = `${edge.source.rfid}->${edge.target.rfid}`;
      const existing = edgeShareMap.get(key);
      if (existing == null || edge.share > existing) {
        edgeShareMap.set(key, edge.share);
      }
    }

    for (const node of dagOrderedNodes) {
      node.sortedChildren = [...node.children].sort((a, b) => {
        const shareA = edgeShareMap.get(`${node.rfid}->${a}`) ?? 0;
        const shareB = edgeShareMap.get(`${node.rfid}->${b}`) ?? 0;
        return shareB - shareA;
      });
    }
  }
}
