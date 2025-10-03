// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InternalCodecNode} from './InternalCodecNode';
import {InternalGraphNode} from './InternalGraphNode';
import type {RF_edgeId, RF_nodeId} from './types';
import {NodeType} from './types';
import type {RF_graphId} from './types';
import {InternalEdge} from './InternalEdge';
import {Stream} from '../../models/Stream';
import type {CodecID} from '../../models/idTypes';
import {ZL_GraphType} from '../../models/idTypes';
import type {SerializedStreamdump} from '../../interfaces/SerializedStreamdump';
import {CodecDag} from './CodecDag';
import {Codec} from '../../models/Codec';
import {Graph} from '../../models/Graph';
import {InsertOnlyJournal} from '../../utils/InsertOnlyJournal';
import type {InternalNode} from './InternalNode';

export class InteractiveStreamdumpGraph {
  private static readonly ROOT_CODEC_ID: CodecID = 0 as CodecID;

  private codecs: InternalCodecNode[] = [];
  private streams: Stream[] = [];
  private graphs: Graph[] = [];
  private codecDag: CodecDag | null = null;

  // private codecViewModels = new Map<RF_codecId, InternalCodecNode>();
  private graphViewModels = new Map<RF_graphId, InternalGraphNode>();
  private edgeViewModels = new Map<RF_edgeId, InternalEdge>();
  //   private streamIdToInputCodecs = new Map<StreamID, number[]>();

  constructor(obj: SerializedStreamdump, isDefaultCollapsed = false) {
    this.buildFromSerialized(obj);
    this.markLargestCompressionPath(this.codecs[InteractiveStreamdumpGraph.ROOT_CODEC_ID].id, this.streams);
    if (isDefaultCollapsed) {
      this.startupStandardGraphAndSuccessorsCollapse();
    }
  }

  // Function that runs once on startup of a graph, to build the proper relationships between streams, codecs, and function graphs
  private buildFromSerialized(obj: SerializedStreamdump): void {
    const intermediateCodecs = obj.codecs.map((codec, idx) => Codec.fromObject(codec, idx));
    this.streams = obj.streams.map((stream, idx) => Stream.fromObject(stream, idx));
    this.graphs = obj.graphs.map((graph, idx) => Graph.fromObject(graph, idx));

    // populate graph-codec relationships
    this.graphs.forEach((graph) => {
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

    // Temporary hack: null out all leaf streams that don't have a target codec.
    // Leaf codecs technically generate a store stream that has no target.
    for (let i = 0; i < this.streams.length; i++) {
      const stream = this.streams[i];
      if (stream.targetCodec == Stream.NO_TARGET) {
        console.assert(stream.sourceCodec != Stream.NO_SOURCE);
        intermediateCodecs[stream.sourceCodec].outputStreams = intermediateCodecs[
          stream.sourceCodec
        ].outputStreams.filter((val) => {
          return val !== stream.streamId;
        });
        delete this.streams[i];
      }
    }

    // Temporary hack: null out the 0th stream, which is the root stream.
    // The root codec is assumed to have no input streams
    console.assert(this.streams[0].targetCodec == InteractiveStreamdumpGraph.ROOT_CODEC_ID);
    console.assert(intermediateCodecs[InteractiveStreamdumpGraph.ROOT_CODEC_ID].inputStreams.length === 1);
    console.assert(intermediateCodecs[InteractiveStreamdumpGraph.ROOT_CODEC_ID].inputStreams[0] === 0);
    delete this.streams[0];
    {
      const codec = intermediateCodecs[InteractiveStreamdumpGraph.ROOT_CODEC_ID];
      intermediateCodecs[InteractiveStreamdumpGraph.ROOT_CODEC_ID] = new Codec(
        codec.id,
        codec.name,
        codec.cType,
        codec.cID,
        codec.cHeaderSize,
        codec.cFailureString,
        codec.cLocalParams,
        [], // null out inputStreams
        codec.outputStreams,
        codec.owningGraph,
      );
    }

    // Create the graph view models
    this.graphs.forEach((graph) => {
      this.graphViewModels.set(graph.rfId, new InternalGraphNode(graph.rfId, NodeType.Graph, graph));
    });

    // Create the codec node view models
    this.codecs = intermediateCodecs.map((codec) => {
      const owningGraph = codec.owningGraph;
      const graphViewModel = owningGraph == null ? null : this.graphViewModels.get(this.graphs[owningGraph].rfId)!;
      return new InternalCodecNode(codec.rfId, NodeType.Codec, codec, graphViewModel);
    });

    // populate codecDag
    this.codecDag = new CodecDag(this.codecs, this.streams);

    // Create the edges view models for codec -> codec edges
    for (const stream of this.streams) {
      if (stream == undefined) {
        continue; // consequence of the hack
      }
      const sourceCodec = this.codecs[stream.sourceCodec];
      const targetCodec = this.codecs[stream.targetCodec];
      const label =
        `#${stream.outputIdx} (${stream.rfId}) | ${stream.streamTypeToString()}\n` +
        `${stream.cSize} [${stream.share.toFixed(2)}%]\n` +
        `${stream.numElts} [${stream.eltWidth}]`;
      // Make edge
      this.edgeViewModels.set(
        stream.rfId,
        new InternalEdge(
          stream.rfId,
          stream,
          this.codecs[sourceCodec.id],
          this.codecs[targetCodec.id],
          'source',
          'target',
          label,
          'custom',
          {stroke: 'black', strokeWidth: 2},
        ),
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
            const proxyEdge = new InternalEdge(
              proxyId,
              stream,
              sourceGraph,
              targetGraph,
              'source',
              'target',
              originalEdge.label,
              originalEdge.type,
              originalEdge.style,
            );
            sourceGraph.outgoingEdges.push(proxyEdge);
            targetGraph.incomingEdges.push(proxyEdge);
          }
          {
            const proxyId = `proxy-${sourceGraph.rfid}-${stream.rfId}-${targetCodec.rfid}`;
            sourceGraph.outgoingEdges.push(
              new InternalEdge(
                proxyId,
                stream,
                sourceGraph,
                targetCodec,
                'source',
                originalEdge.targetHandle,
                originalEdge.label,
                originalEdge.type,
                originalEdge.style,
              ),
            );
          }
          {
            const proxyId = `proxy-${sourceCodec.rfid}-${stream.rfId}-${targetGraph.rfid}`;
            targetGraph.incomingEdges.push(
              new InternalEdge(
                proxyId,
                stream,
                sourceCodec,
                targetGraph,
                originalEdge.sourceHandle,
                'target',
                originalEdge.label,
                originalEdge.type,
                originalEdge.style,
              ),
            );
          }
        }
      } else if (sourceGraph != null) {
        const proxyId = `proxy-${sourceGraph.rfid}-${stream.rfId}-${targetCodec.rfid}`;
        sourceGraph.outgoingEdges.push(
          new InternalEdge(
            proxyId,
            stream,
            sourceGraph,
            targetCodec,
            'source',
            originalEdge.targetHandle,
            originalEdge.label,
            originalEdge.type,
            originalEdge.style,
          ),
        );
      } else if (targetGraph != null) {
        // const sourceCodecRfId = this.codecs[sourceCodecId].rfId;
        // const targetGraphRfId = this.graphs[targetGraphId].rfId;
        const proxyId = `proxy-${sourceCodec.rfid}-${stream.rfId}-${targetGraph.rfid}`;
        targetGraph.incomingEdges.push(
          new InternalEdge(
            proxyId,
            stream,
            sourceCodec,
            targetGraph,
            originalEdge.sourceHandle,
            'target',
            originalEdge.label,
            originalEdge.type,
            originalEdge.style,
          ),
        );
      }
    });
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

    // temporary filter to de-dup multiple edges
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
        const totShare = edges.reduce((acc, item) => acc + item.stream.share, 0);
        const totCSize = edges.reduce((acc, item) => acc + item.stream.cSize, 0);
        const coalescedEdge = new InternalEdge(
          edges[0].id,
          edges[0].stream,
          edges[0].source,
          edges[0].target,
          edges[0].sourceHandle,
          edges[0].targetHandle,
          `#- (-) | Multiple edges\n${totCSize} [${totShare.toFixed(2)}%]\n- [-]`,
          'custom',
          edges[0].style,
        );
        dedupedEdges.push(coalescedEdge);
      }
    }

    console.log(visibleEdgeSet);
    console.log(visibleNodeSet);
    return {dagOrderedNodes: Array.from(visibleNodeSet), edges: dedupedEdges};
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
    // const codecId = codec.rfid;
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
      const graphsToCheck = new Set<RF_graphId>();
      const childCodecs = this.getCodecDescendantsToHide(codec, new Set<InternalCodecNode>());
      childCodecs.forEach((childCodec) => {
        const graph = childCodec.parentGraph;
        // If a descendant node to hide is part of a collapsed function graph, mark the collapsed graph to not be visible,
        // so that when we expand the ancestor node, we preserve the collapsed graph state
        if (graph != null) {
          if (graph.isCollapsed) {
            graph.isVisible = false;
          } else {
            graphsToCheck.add(graph.rfid as RF_graphId);
          }
        }
        childCodec.isVisible = false;
      });

      // For function graphs that aren't collapsed, if all codecs within it are hidden, we want to hide the function graph as well
      graphsToCheck.forEach((graphId) => {
        const graph = this.graphViewModels.get(graphId)!;
        const allNodesHidden = graph.graph.codecIDs.every((codecId) => !this.codecs[codecId].isVisible);
        if (allNodesHidden) {
          graph.isVisible = false;
        }
      });
    }

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
        // const owningGraph = this.graphs[childGraphId];
        const owningGraphViewModel = this.graphViewModels.get(childGraph.rfid as RF_graphId)!;
        owningGraphViewModel.isVisible = true; // Make sure the collapsed graph is visible
        owningGraphViewModel.isCollapsed = true;
        newlyVisibleNodes.push(owningGraphViewModel.rfid);
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

    return newlyVisibleNodes;
  }

  // Helper function to display codecs in a function graph without overriding any collapsed odecs within the function graph
  displayCodecsInGraph(
    codec: InternalCodecNode,
    graphId: RF_graphId,
    visited: Set<InternalCodecNode>,
    newlyVisibleNodes: RF_nodeId[],
  ) {
    codec.isVisible = true;
    newlyVisibleNodes.push(codec.rfid);
    if (codec.isCollapsed || visited.has(codec)) {
      return;
    }
    const graphViewModel = this.graphViewModels.get(graphId)!;
    this.codecDag!.getChildren(codec).forEach((childCodec) => {
      if (childCodec.parentGraph === graphViewModel) {
        this.displayCodecsInGraph(childCodec, graphId, visited, newlyVisibleNodes);
      }
    });
  }

  // Function to support the feature of collapsing/expanding a function graph
  toggleGraphCollapse(graph: InternalGraphNode): RF_nodeId[] {
    const graphId = graph.rfid;
    const newlyVisibleNodes: RF_nodeId[] = [];
    // Expanding this function graph
    if (this.graphViewModels.get(graphId as RF_graphId)!.isCollapsed) {
      graph.isCollapsed = false;
      this.displayCodecsInGraph(
        this.codecs[graph.graph.codecIDs[0]],
        graphId as RF_graphId,
        new Set<InternalCodecNode>(),
        newlyVisibleNodes,
      );
    }
    // Collapsing this function graph
    else {
      graph.isCollapsed = true;
      // Hide all codecs within the function graph
      graph.graph.codecIDs.forEach((codecId) => {
        this.codecs[codecId].isVisible = false;
      });
      // Add the function graph itself as a newly visible node as we want the screen to focus on it
      newlyVisibleNodes.push(graphId);
    }

    return newlyVisibleNodes;
  }

  // collapses the graph component and all its successors into one node
  toggleGraphHide(graph: InternalGraphNode): RF_nodeId[] {
    let nodesToFocus = this.toggleSubgraphCollapse(this.codecs[graph.graph.codecIDs[0]]);
    if (graph.isCollapsed) {
      graph.isCollapsed = false;
      nodesToFocus.push(graph.rfid);
    } else {
      graph.isCollapsed = true;
      nodesToFocus = [graph.rfid];
    }
    console.assert(graph.isVisible);
    return nodesToFocus;
  }

  // Function to support the feature of collapsing/expanding all standard graphs
  toggleAllStandardGraphs(isCollapsed: boolean) {
    this.graphViewModels.forEach((graph, _) => {
      if (
        graph.isVisible &&
        graph.isCollapsed !== isCollapsed &&
        graph.graph.gType === ZL_GraphType.ZL_GraphType_standard
      ) {
        this.toggleGraphHide(graph);
      }
    });
  }

  startupStandardGraphAndSuccessorsCollapse() {
    // const newlyVisibleNodes: RF_codecId[] = []; // So that on startup, our graph is centered

    // Hide graphs in dag order
    const order = this.codecDag!.dagOrder();
    for (const codec of order) {
      // find the owning graph view model
      if (codec.parentGraph == null) {
        continue;
      }
      // const graph = this.graphs[this.codecs[codecId].owningGraph];
      const graphModel = codec.parentGraph;
      if (!graphModel.isVisible) {
        console.assert(!codec.isVisible);
        continue;
      }
      if (graphModel.isCollapsed) {
        continue;
      }
      // catch stragglers graphs that haven't been collapsed (for some reason)
      // if (!codecModel.isVisible && !graphModel.isCollapsed) {
      //   console.log('found straggler ' + codecModel.id + ' ' + graphModel.id);
      //   graphModel.isVisible = false;
      //   continue;
      // }

      // we've found a graph that should potentially be collapsed
      if (graphModel.graph.gType === ZL_GraphType.ZL_GraphType_standard) {
        const rootCodec = this.codecs[graphModel.graph.codecIDs[0]];
        console.assert(rootCodec === codec);
        this.toggleGraphHide(graphModel);
        codec.isVisible = false;
        graphModel.isCollapsed = true;
      }
    }
  }
}
