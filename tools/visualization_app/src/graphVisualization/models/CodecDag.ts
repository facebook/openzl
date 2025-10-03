// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {CodecID} from '../../models/idTypes';
import type {InternalCodecNode} from './InternalCodecNode';
import type {Stream} from '../../models/Stream';

/**
 * A DAG of nodes for easier traversal of the graph. This is necessary because
 * the @ref Codec and @ref Stream classes aren't meant to be used directly for
 * graph traversal.
 */
export class CodecDag {
  private readonly adjList: Map<CodecID, Set<CodecID>>;
  private readonly codecList: InternalCodecNode[]; // list of codecs in ID order for lookup purposes
  private readonly dagOrderList: InternalCodecNode[];

  constructor(codecs: InternalCodecNode[], streams: Stream[]) {
    this.adjList = new Map();
    this.codecList = codecs;
    codecs.forEach((codec) => {
      const childSet = new Set<CodecID>();
      for (const streamId of codec.outputStreams) {
        if (streams[streamId] == undefined) {
          // because of temp hack
          continue;
        }
        const targetCodecId = streams[streamId].targetCodec;
        childSet.add(targetCodecId);
      }
      this.adjList.set(codec.id, childSet);
    });
    console.assert(codecs.length === this.adjList.size);

    // Ensure codec at index 0 is the root
    console.assert(codecs[0].inputStreams.length === 0);

    // generate dag order
    this.dagOrderList = this.findTopologicalSort(this.adjList).map((id) => this.codecList[id]);
  }

  dagOrder(): InternalCodecNode[] {
    return this.dagOrderList;
  }

  reverseDagOrder(): InternalCodecNode[] {
    return Array.from(this.dagOrderList).reverse();
  }

  getChildren(codec: InternalCodecNode): InternalCodecNode[] {
    const ids = Array.from(this.adjList.get(codec.id) ?? []);
    return ids.map((id) => this.codecList[id]);
  }

  private findTopologicalSort(adjList: Map<CodecID, Set<CodecID>>): CodecID[] {
    const tSort: CodecID[] = [];
    const inDegree = new Map<CodecID, number>();

    // find in-degree for each vertex
    adjList.forEach((edges, vertex) => {
      // If vertex is not in the map, add it to the inDegree map
      if (!inDegree.has(vertex)) {
        inDegree.set(vertex, 0);
      }

      edges.forEach((targetNodeId) => {
        // Increase the inDegree for each edge target
        if (inDegree.has(targetNodeId)) {
          inDegree.set(targetNodeId, inDegree.get(targetNodeId)! + 1);
        } else {
          inDegree.set(targetNodeId, 1);
        }
      });
    });

    // Queue for holding vertices that has 0 inDegree Value
    const queue: CodecID[] = [];
    inDegree.forEach((degree, vertex) => {
      // Add vertices with inDegree 0 to the queue
      if (degree == 0) {
        queue.push(vertex);
      }
    });

    // Traverse through the leaf vertices
    while (queue.length > 0) {
      const current = queue.shift()!;
      tSort.push(current);
      // Mark the current vertex as visited and decrease the inDegree for the edges of the vertex
      // Imagine we are deleting this current vertex from our graph
      if (adjList.has(current)) {
        adjList.get(current)!.forEach((edge) => {
          if (inDegree.has(edge) && inDegree.get(edge)! > 0) {
            // Decrease the inDegree for the adjacent vertex
            const newDegree = inDegree.get(edge)! - 1;
            inDegree.set(edge, newDegree);

            // if inDegree becomes zero, we found new leaf node.
            // Add to the queue to traverse through its edges
            if (newDegree == 0) {
              queue.push(edge);
            }
          }
        });
      }
    }
    return tSort;
  }
}
