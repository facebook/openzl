// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Graph} from '../../models/Graph';
import {InternalNode} from './InternalNode';
import {InternalEdge} from './InternalEdge';
import {NodeType} from './types';
import type {RF_codecId, RF_graphId} from './types';

export class InternalGraphNode extends InternalNode {
  graph: Graph;
  codecIds: RF_codecId[] = [];
  // isCollapsed: boolean = false;
  // isVisible: boolean = true;
  incomingEdges: InternalEdge[] = [];
  outgoingEdges: InternalEdge[] = [];

  constructor(rfid: RF_graphId, type: NodeType, graph: Graph) {
    super(rfid, type);
    this.rfid = rfid;
    this.graph = graph;
    this.codecIds = graph.codecIDs.map((codecID) => `T${codecID}` as RF_codecId);
  }
}
