// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InternalCodecNode} from './InternalCodecNode';
import {InternalGraphNode} from './InternalGraphNode';
import type {RF_edgeId, RF_nodeId} from './types';
import {NodeType} from './types';
import {InternalEdge} from './InternalEdge';
import {Stream} from '../../models/Stream';
import type {CodecID} from '../../models/idTypes';
import {ZL_GraphType} from '../../models/idTypes';
import type {SerializedChunk} from '../../interfaces/SerializedChunk';
import {CodecDag} from './CodecDag';
import {Codec} from '../../models/Codec';
import {Graph} from '../../models/Graph';
import {InsertOnlyJournal} from '../../utils/InsertOnlyJournal';
import type {InternalNode} from './InternalNode';
import {InternalSegmenterNode} from './InternalSegmenterNode';

export class InteractiveChunkGraph {
  private static readonly ROOT_CODEC_ID: CodecID = 0 as CodecID;

  private codecs: InternalCodecNode[] = [];
  private streams: Stream[] = [];
  private graphs: InternalGraphNode[] = [];
  private codecDag: CodecDag | null = null;
  private codecByRfid = new Map<RF_nodeId, InternalCodecNode>();
  private graphByRfid = new Map<RF_nodeId, InternalGraphNode>();

  private edgeViewModels = new Map<RF_edgeId, InternalEdge>();

  constructor(obj: SerializedChunk, isDefaultCollapsed = false) {
    this.buildFromSerialized(obj);
    this.markLargestCompressionPath(this.codecs[InteractiveChunkGraph.ROOT_CODEC_ID].id, this.streams);
    if (isDefaultCollapsed) {
      this.startupStandardGraphAndSuccessorsCollapse();
    }
  }

  // Function that runs once on startup of a graph, to build the proper relationships between streams, codecs, and function graphs
  private buildFromSerialized(obj: SerializedChunk): void {
    const intermediateCodecs = obj.codecs.map((codec, idx) => Codec.fromObject(codec, idx));
    this.streams = obj.streams.map((stream, idx) => Stream.fromObject(stream, idx));
    const intermediateGraphs = obj.graphs.map((graph, idx) => Graph.fromObject(graph, idx));

    // populate graph-codec relationships
    intermediateGraphs.forEach((graph) => {
      graph.codecIDs.forEach((codecId) => {
        intermediateCodecs[codecId].owningGraph = graph.gNum;
      });
    });
    // populate stream source/target
    intermediateCodecs.forEach((codec) => {
      codec.outputStreams.forEach((streamId) => {
        this.streams[streamId].sourceCodec = codec.id;
      });
      codec.inputStreams.forEach((streamId) => {
        this.streams[streamId].targetCodec = codec.id;
      });
    });

    // Create the graph view models
    this.graphs = intermediateGraphs.map((graph) => {
      return new InternalGraphNode(graph.rfId, NodeType.Graph, graph);
    });

    // Create the codec node view models
    this.codecs = intermediateCodecs.map((codec) => {
      const owningGraphId = codec.owningGraph;
      const graphViewModel = owningGraphId == null ? null : this.graphs[owningGraphId];
      const codecViewModel =
        codec.name === 'segmenter'
          ? new InternalSegmenterNode(codec.rfId, NodeType.Segmenter, codec, graphViewModel)
          : new InternalCodecNode(codec.rfId, NodeType.Codec, codec, graphViewModel);
      if (graphViewModel !== null) {
        graphViewModel.codecs.push(codecViewModel);
      }
      return codecViewModel;
    });

    // Verification
    console.assert(this.graphs.length === intermediateGraphs.length);
    for (let i = 0; i < this.graphs.length; i++) {
      console.assert(this.graphs[i].rfid === intermediateGraphs[i].rfId);
      console.assert(this.graphs[i].codecs.length === intermediateGraphs[i].codecIDs.length);
      for (let j = 0; j < this.graphs[i].codecs.length; j++) {
        console.assert(this.graphs[i].codecs[j].id === intermediateGraphs[i].codecIDs[j]);
      }
    }

    // populate lookup maps and DAG
    this.codecByRfid = new Map(this.codecs.map((c) => [c.rfid, c]));
    this.graphByRfid = new Map(this.graphs.map((g) => [g.rfid, g]));
    this.codecDag = new CodecDag(this.codecs, this.streams);

    // Create the edges view models for codec -> codec edges
    for (const stream of this.streams) {
      const sourceCodec = this.codecs[stream.sourceCodec];
      const targetCodec = this.codecs[stream.targetCodec];
      // Make edge
      this.edgeViewModels.set(
        stream.rfId,
        InternalEdge.constructFromStream(stream.rfId, this.codecs[sourceCodec.id], this.codecs[targetCodec.id], stream),
      );
    }

    // Also create proxy edges for graph <-> codec edges, displayed when graph components are collapsed
    this.buildGraphEdgeMaps();
  }

  // Function to build the visual around the largest data size path within the tree
  private markLargestCompressionPath(currCodecId: CodecID, streams: Stream[]): void {
    const currCodec = this.codecs[currCodecId];
    if (!currCodec) {
      return;
    }
    currCodec.inLargestCompressionPath = true; // Mark as part of largest path
    // Find largest stream
    let largestStream: Stream | null = null;
    currCodec.outputStreams.forEach((streamId) => {
      if (largestStream === null) {
        largestStream = streams[streamId];
      } else if (largestStream.cSize < streams[streamId].cSize) {
        largestStream = streams[streamId];
      }
    });

    if (largestStream !== null) {
      const streamStream = largestStream as Stream;
      // Traverse down this path to keep going
      const nextCodecId = streamStream.targetCodec;
      const nextCodec = this.codecs[nextCodecId];
      if (nextCodec.parentGraph !== null) {
        nextCodec.parentGraph.inLargestCompressionPath = true;
      }
      this.markLargestCompressionPath(nextCodec.id, streams);
    }
  }

  // Function to build the relation between in-going and out-going edges to a function graph, so that when a graph is collapsed
  // and to be treated like a node, we can create the appropriate edges codec->graph and graph->codec
  private buildGraphEdgeMaps(): void {
    this.streams.forEach((stream) => {
      const originalEdge = this.edgeViewModels.get(stream.rfId)!;
      const sourceCodec = this.codecs[stream.sourceCodec];
      const targetCodec = this.codecs[stream.targetCodec];
      const sourceGraph = sourceCodec.parentGraph;
      const targetGraph = targetCodec.parentGraph;

      if (sourceGraph != null && targetGraph != null) {
        // Separate function graphs connected to each other
        if (sourceGraph != targetGraph) {
          // add graph-to-graph and codec-to-graph (in both directions)
          {
            const proxyId = `proxy-${sourceGraph.rfid}-${stream.rfId}-${targetGraph.rfid}`;
            const proxyEdge = InternalEdge.constructFromInternalEdge(
              proxyId as RF_edgeId,
              sourceGraph,
              targetGraph,
              originalEdge,
            );
            sourceGraph.outgoingEdges.push(proxyEdge);
            targetGraph.incomingEdges.push(proxyEdge);
          }
          {
            const proxyId = `proxy-${sourceGraph.rfid}-${stream.rfId}-${targetCodec.rfid}`;
            sourceGraph.outgoingEdges.push(
              InternalEdge.constructFromInternalEdge(proxyId as RF_edgeId, sourceGraph, targetCodec, originalEdge),
            );
          }
          {
            const proxyId = `proxy-${sourceCodec.rfid}-${stream.rfId}-${targetGraph.rfid}`;
            targetGraph.incomingEdges.push(
              InternalEdge.constructFromInternalEdge(proxyId as RF_edgeId, sourceCodec, targetGraph, originalEdge),
            );
          }
        }
      } else if (sourceGraph != null) {
        const proxyId = `proxy-${sourceGraph.rfid}-${stream.rfId}-${targetCodec.rfid}`;
        sourceGraph.outgoingEdges.push(
          InternalEdge.constructFromInternalEdge(proxyId as RF_edgeId, sourceGraph, targetCodec, originalEdge),
        );
      } else if (targetGraph != null) {
        const proxyId = `proxy-${sourceCodec.rfid}-${stream.rfId}-${targetGraph.rfid}`;
        targetGraph.incomingEdges.push(
          InternalEdge.constructFromInternalEdge(proxyId as RF_edgeId, sourceCodec, targetGraph, originalEdge),
        );
      }
    });
  }

  contains(node: InternalCodecNode | InternalGraphNode): boolean {
    if (node instanceof InternalGraphNode) {
      return this.graphs.includes(node);
    }
    return this.codecs.includes(node);
  }

  // Helper function to identify all the visible nodes, edges, and graphs to display on the graph upon collapse/expanding of a component on the graph
  //
  // TODO [T223424749] (edge case to consider): if a child graph is collapsed, and the parent graph collapses, we need to add logic to connect a
  // collapsed graph to a collapsed graph. This is because we only have logic from a collapsed graph to CODEC nodes, not graph nodes.
  getVisibleStreamdumpGraph(): {
    dagOrderedNodes: InternalNode[];
    edges: InternalEdge[];
  } {
    // Walk down the graph in DAG order. Add visible nodes and graphs
    const order = this.codecDag!.dagOrder();
    const visibleNodeSet = new InsertOnlyJournal<InternalNode>();
    const visibleEdgeSet = new InsertOnlyJournal<InternalEdge>();
    for (const codec of order) {
      const maybeGraph = codec.parentGraph;
      // add codec and owning graph, if visible
      // add the owning graph first so it's in the list before the subgraph codecs
      if (maybeGraph && maybeGraph.isVisible) {
        visibleNodeSet.insert(maybeGraph);
      }
      if (codec.isVisible) {
        visibleNodeSet.insert(codec);
      }

      // add child edges. if codec is hidden, but owning graph isn't, add a proxy edge to/from the visible graph
      for (const streamId of codec.outputStreams) {
        const childCodecId = this.streams[streamId].targetCodec;
        console.assert(childCodecId !== Stream.NO_TARGET);
        const childCodec = this.codecs[childCodecId];
        const maybeChildGraph = childCodec.parentGraph;
        if (codec.isVisible) {
          if (childCodec.isVisible) {
            visibleEdgeSet.insert(this.edgeViewModels.get(this.streams[streamId].rfId)!);
          } else if (maybeChildGraph && maybeChildGraph.isVisible && maybeChildGraph !== maybeGraph) {
            const possibleEdges = maybeChildGraph.incomingEdges.filter((edge) => edge.source === codec);
            console.assert(possibleEdges.length === 1);
            visibleEdgeSet.insert(possibleEdges[0]);
          }
        } else if (maybeGraph && maybeGraph.isVisible) {
          if (childCodec.isVisible) {
            const possibleEdges = maybeGraph.outgoingEdges.filter((edge) => edge.target == childCodec);
            console.assert(possibleEdges.length === 1);
            visibleEdgeSet.insert(possibleEdges[0]);
          } else if (maybeChildGraph && maybeChildGraph.isVisible && maybeChildGraph !== maybeGraph) {
            const possibleEdges = maybeChildGraph.incomingEdges.filter((edge) => edge.source === maybeGraph);
            console.assert(possibleEdges.length === 1);
            visibleEdgeSet.insert(possibleEdges[0]);
          }
        }
      }
    }

    if (0) {
      // filter to de-dup multiple edges
      const edgeMap = new Map<string, InternalEdge[]>();
      for (const edge of visibleEdgeSet) {
        const key = `${edge.source.rfid}-${edge.target.rfid}`;
        const edges = edgeMap.get(key);
        if (edges) {
          edges.push(edge);
        } else {
          edgeMap.set(key, [edge]);
        }
      }
      const dedupedEdges = [];
      for (const edges of edgeMap.values()) {
        if (edges.length === 1) {
          dedupedEdges.push(edges[0]);
        } else {
          const totShare = edges.reduce((acc, item) => acc + item.share, 0);
          const totCSize = edges.reduce((acc, item) => acc + item.cSize, 0);
          const coalescedEdge = new InternalEdge(
            '-' as RF_edgeId,
            edges[0].streamId,
            edges[0].type,
            -1,
            -1,
            -1,
            totCSize,
            totShare,
            -1,
            undefined, // streamPreview: empty for coalesced edges
            edges[0].source,
            edges[0].target,
          );
          dedupedEdges.push(coalescedEdge);
        }
      }
    }

    console.log(visibleEdgeSet);
    console.log(visibleNodeSet);
    return {dagOrderedNodes: Array.from(visibleNodeSet), edges: Array.from(visibleEdgeSet)};
  }

  getCodecChildren(codec: InternalCodecNode): InternalCodecNode[] {
    return this.codecDag ? this.codecDag.getChildren(codec) : [];
  }

  // Given the visible node that represents a codec or graph, find its visible
  // navigable neighbors using the DAG and link them via parents/children.
  private rebuildNavlinksForCodec(codec: InternalCodecNode): void {
    codec.parents = [];
    codec.children = [];
    const childShareMap = new Map<RF_nodeId, number>();

    // Link to visible DAG children (or their collapsed parent graph)
    for (const child of this.codecDag!.getChildren(codec)) {
      // Find the edge share from codec to child
      const edgeShare = this.getEdgeShare(codec, child);
      if (child.isVisible) {
        this.addNavLink(codec, child);
        childShareMap.set(child.rfid, edgeShare);
      } else if (child.parentGraph && child.parentGraph.isVisible && child.parentGraph.isCollapsed) {
        this.addNavLink(codec, child.parentGraph);
        const existing = childShareMap.get(child.parentGraph.rfid) ?? 0;
        childShareMap.set(child.parentGraph.rfid, Math.max(existing, edgeShare));
      }
    }

    // Link to visible DAG parents (or their collapsed parent graph)
    for (const parent of this.codecDag!.getParents(codec)) {
      if (parent.isVisible) {
        this.addNavLink(parent, codec);
      } else if (parent.parentGraph && parent.parentGraph.isVisible && parent.parentGraph.isCollapsed) {
        this.addNavLink(parent.parentGraph, codec);
      }
    }

    // Sort children by descending edge share
    codec.sortedChildren = [...codec.children].sort((a, b) => {
      return (childShareMap.get(b) ?? 0) - (childShareMap.get(a) ?? 0);
    });
  }

  private rebuildNavlinksForGraph(graph: InternalGraphNode): void {
    graph.parents = [];
    graph.children = [];
    const childShareMap = new Map<RF_nodeId, number>();

    for (const edge of graph.incomingEdges) {
      if (!edge.source.isVisible) continue;
      if (edge.source instanceof InternalGraphNode && !edge.source.isCollapsed) continue;
      this.addNavLink(edge.source, graph);
    }
    for (const edge of graph.outgoingEdges) {
      if (!edge.target.isVisible) continue;
      if (edge.target instanceof InternalGraphNode && !edge.target.isCollapsed) continue;
      this.addNavLink(graph, edge.target);
      const existing = childShareMap.get(edge.target.rfid) ?? 0;
      childShareMap.set(edge.target.rfid, Math.max(existing, edge.share));
    }

    graph.sortedChildren = [...graph.children].sort((a, b) => {
      return (childShareMap.get(b) ?? 0) - (childShareMap.get(a) ?? 0);
    });
  }

  rebuildNavlinksFor(nodeIds: RF_nodeId[]): void {
    const toRebuild = new Set<RF_nodeId>(nodeIds);

    // Also rebuild parents of changed nodes so their children arrays stay correct
    for (const rfid of nodeIds) {
      const codec = this.codecByRfid.get(rfid);
      if (codec) {
        for (const parent of this.codecDag!.getParents(codec)) {
          if (parent.isVisible) toRebuild.add(parent.rfid);
          else if (parent.parentGraph?.isVisible) toRebuild.add(parent.parentGraph.rfid);
        }
        continue;
      }
      const graph = this.graphByRfid.get(rfid);
      if (graph) {
        // Rebuild parents of the graph's root codec
        const rootCodec = graph.codecs[0];
        if (rootCodec) {
          for (const parent of this.codecDag!.getParents(rootCodec)) {
            if (parent.isVisible) toRebuild.add(parent.rfid);
            else if (parent.parentGraph?.isVisible) toRebuild.add(parent.parentGraph.rfid);
          }
        }
      }
    }

    for (const rfid of toRebuild) {
      const codec = this.codecByRfid.get(rfid);
      if (codec) {
        this.rebuildNavlinksForCodec(codec);
        continue;
      }
      const graph = this.graphByRfid.get(rfid);
      if (graph) {
        this.rebuildNavlinksForGraph(graph);
      }
    }
  }

  private getEdgeShare(source: InternalCodecNode, target: InternalCodecNode): number {
    for (const streamId of source.outputStreams) {
      const stream = this.streams[streamId];
      if (stream.targetCodec === target.id) {
        const edge = this.edgeViewModels.get(stream.rfId);
        if (edge) return edge.share;
      }
    }
    return 0;
  }

  private addNavLink(parent: InternalNode, child: InternalNode): void {
    if (!parent.children.includes(child.rfid)) {
      parent.children.push(child.rfid);
    }
    if (!child.parents.includes(parent.rfid)) {
      child.parents.push(parent.rfid);
    }
  }

  // Function to get descendants of a codec under some defined recursion condition
  // Note: this is not tail-recursive. This may eventually become problematic for super deep graphs...
  private getDescendants(
    codec: InternalCodecNode,
    visitedChildren: Set<InternalCodecNode>,
    shouldRecurse: (childCodecId: InternalCodecNode) => boolean,
  ): Set<InternalCodecNode> {
    const childrenCodecs = this.codecDag!.getChildren(codec);
    childrenCodecs.forEach((child) => {
      if (!visitedChildren.has(child)) {
        visitedChildren.add(child);
        if (shouldRecurse(child)) {
          this.getDescendants(child, visitedChildren, shouldRecurse);
        }
      }
    });
    return visitedChildren;
  }

  private getCodecDescendantsToExpand(
    codec: InternalCodecNode,
    visitedDescendants: Set<InternalCodecNode>,
  ): Set<InternalCodecNode> {
    return this.getDescendants(codec, visitedDescendants, (childCodec) => !childCodec.isCollapsed);
  }

  private getCodecDescendantsToHide(
    codec: InternalCodecNode,
    visitedDescendants: Set<InternalCodecNode>,
  ): Set<InternalCodecNode> {
    return this.getDescendants(codec, visitedDescendants, (_childCodecId) => true);
  }

  toggleSubgraphCollapse(codec: InternalCodecNode): RF_nodeId[] {
    const newlyVisibleNodes: RF_nodeId[] = [];

    // Expanding this node's subgraph
    if (codec.isCollapsed) {
      codec.isCollapsed = false;
      // Get all children that were previously hidden by this codec's collapse
      const childCodecs = this.getCodecDescendantsToExpand(codec, new Set<InternalCodecNode>());
      childCodecs.forEach((childCodec) => {
        const graph = childCodec.parentGraph;
        if (graph != null) {
          graph.isVisible = true;
          // If the function graph the codec is in is collapsed, we only want to display the collapsed function graph as a
          // node, and not the codecs within the function graph
          if (graph.isCollapsed) {
            newlyVisibleNodes.push(graph.rfid);
          } else {
            childCodec.isVisible = true;
            newlyVisibleNodes.push(childCodec.rfid);
          }
        } else {
          childCodec.isVisible = true;
          newlyVisibleNodes.push(childCodec.rfid);
        }
      });

      // Add the expanded codec to the set of ndoes the screen should pan over
      newlyVisibleNodes.push(codec.rfid);
    }
    // Collapsing this node's subgraph
    else {
      codec.isCollapsed = true;
      // Focus on just the codec that is being collapsed
      newlyVisibleNodes.push(codec.rfid);
      const graphsToCheck = new Set<InternalGraphNode>();
      const childCodecs = this.getCodecDescendantsToHide(codec, new Set<InternalCodecNode>());
      childCodecs.forEach((childCodec) => {
        const graph = childCodec.parentGraph;
        // If a descendant node to hide is part of a collapsed function graph, mark the collapsed graph to not be visible,
        // so that when we expand the ancestor node, we preserve the collapsed graph state
        if (graph != null) {
          if (graph.isCollapsed) {
            graph.isVisible = false;
            graph.parents = [];
            graph.children = [];
            graph.sortedChildren = [];
          } else {
            graphsToCheck.add(graph);
          }
        }
        childCodec.isVisible = false;
        childCodec.parents = [];
        childCodec.children = [];
        childCodec.sortedChildren = [];
      });

      // For function graphs that aren't collapsed, if all codecs within it are hidden, we want to hide the function graph as well
      graphsToCheck.forEach((graph) => {
        const allNodesHidden = graph.codecs.every((codec) => !codec.isVisible);
        if (allNodesHidden) {
          graph.isVisible = false;
          graph.parents = [];
          graph.children = [];
          graph.sortedChildren = [];
        }
      });
    }

    this.rebuildNavlinksFor(newlyVisibleNodes);
    return newlyVisibleNodes;
  }
  // Function to support the feature of level-by-level expansion of the graph
  expandOneLevel(codec: InternalCodecNode): RF_nodeId[] {
    const newlyVisibleNodes: RF_nodeId[] = [];
    codec.isCollapsed = false;
    newlyVisibleNodes.push(codec.rfid);

    this.codecDag!.getChildren(codec).forEach((childCodec) => {
      const childGraph = childCodec.parentGraph;
      // If the child codec is part of a (different) collapsed function graph, display the collapsed function graph, not the child codec
      if (childGraph != null && childGraph != codec.parentGraph) {
        childGraph.isVisible = true; // Make sure the collapsed graph is visible
        childGraph.isCollapsed = true;
        newlyVisibleNodes.push(childGraph.rfid);
      } else {
        childCodec.isVisible = true;
        newlyVisibleNodes.push(childCodec.rfid);
      }
      // Collapse the child if it has children itself to preserve the 1 level expansion
      const childCodecHasChildren = this.codecDag!.getChildren(childCodec).length !== 0;
      if (childCodecHasChildren) {
        childCodec.isCollapsed = true;
      }
    });

    this.rebuildNavlinksFor(newlyVisibleNodes);
    return newlyVisibleNodes;
  }

  // Helper function to display codecs in a function graph without overriding any collapsed odecs within the function graph
  private displayCodecsInGraph(
    codec: InternalCodecNode,
    graph: InternalGraphNode,
    visited: Set<InternalCodecNode>,
    newlyVisibleNodes: RF_nodeId[],
  ) {
    codec.isVisible = true;
    newlyVisibleNodes.push(codec.rfid);
    if (codec.isCollapsed || visited.has(codec)) {
      return;
    }
    this.codecDag!.getChildren(codec).forEach((childCodec) => {
      if (childCodec.parentGraph === graph) {
        this.displayCodecsInGraph(childCodec, graph, visited, newlyVisibleNodes);
      }
    });
  }

  // Function to support the feature of collapsing/expanding a function graph
  toggleGraphCollapse(graph: InternalGraphNode): RF_nodeId[] {
    const newlyVisibleNodes: RF_nodeId[] = [];
    // Expanding this function graph
    if (graph.isCollapsed) {
      graph.isCollapsed = false;
      graph.parents = [];
      graph.children = [];
      graph.sortedChildren = [];
      this.displayCodecsInGraph(graph.codecs[0], graph, new Set<InternalCodecNode>(), newlyVisibleNodes);
    }
    // Collapsing this function graph
    else {
      graph.isCollapsed = true;
      // Hide all codecs within the function graph
      graph.codecs.forEach((codec) => {
        codec.isVisible = false;
        codec.parents = [];
        codec.children = [];
        codec.sortedChildren = [];
      });
      // Add the function graph itself as a newly visible node as we want the screen to focus on it
      newlyVisibleNodes.push(graph.rfid);
    }
    this.rebuildNavlinksFor(newlyVisibleNodes);
    return newlyVisibleNodes;
  }

  // collapses the graph component and all its successors into one node
  toggleGraphHide(graph: InternalGraphNode): RF_nodeId[] {
    let nodesToFocus = this.toggleSubgraphCollapse(graph.codecs[0]);
    if (graph.isCollapsed) {
      graph.isCollapsed = false;
      nodesToFocus.push(graph.rfid);
    } else {
      graph.isCollapsed = true;
      graph.codecs.forEach((codec) => {
        codec.isVisible = false;
        codec.parents = [];
        codec.children = [];
        codec.sortedChildren = [];
      });
      nodesToFocus = [graph.rfid];
    }
    console.assert(graph.isVisible);
    this.rebuildNavlinksFor(nodesToFocus);
    return nodesToFocus;
  }

  // Function to support the feature of collapsing/expanding all standard graphs
  toggleAllStandardGraphs(isCollapsed: boolean) {
    this.graphs.forEach((graph, _) => {
      if (graph.isVisible && graph.isCollapsed !== isCollapsed && graph.gType === ZL_GraphType.ZL_GraphType_standard) {
        this.toggleGraphHide(graph);
      }
    });
  }

  startupStandardGraphAndSuccessorsCollapse() {
    // Hide graphs in dag order
    const order = this.codecDag!.dagOrder();
    for (const codec of order) {
      // find the owning graph view model
      if (codec.parentGraph == null) {
        continue;
      }
      const graph = codec.parentGraph;
      if (!graph.isVisible) {
        console.assert(!codec.isVisible);
        continue;
      }
      if (graph.isCollapsed) {
        continue;
      }

      // we've found a graph that should potentially be collapsed
      if (graph.gType === ZL_GraphType.ZL_GraphType_standard) {
        const rootCodec = graph.codecs[0];
        console.assert(rootCodec === codec);
        this.toggleGraphHide(graph);
        codec.isVisible = false;
        graph.isCollapsed = true;
      }
    }
  }

  // Helper methods for chunk merging
  findCodecByName(name: string): InternalCodecNode | null {
    for (const codec of this.codecs) {
      if (codec.name === name) {
        return codec;
      }
    }
    return null;
  }
}
