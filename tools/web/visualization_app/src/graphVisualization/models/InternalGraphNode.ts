// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Graph} from '../../models/Graph';
import {InternalNode} from './InternalNode';
import {InternalEdge} from './InternalEdge';
import {NodeType} from './types';
import type {RF_graphId} from './types';
import {ZL_GraphType, type GraphID} from '../../models/idTypes';
import type {InternalCodecNode} from './InternalCodecNode';
import type {LocalParamInfo} from '../../models/LocalParamInfo';

export class InternalGraphNode extends InternalNode {
  readonly id: GraphID;

  // traced properties
  readonly gType: ZL_GraphType;
  readonly gName: string;
  readonly gFailureString: string;
  readonly gLocalParams: LocalParamInfo;

  // derived graph properties
  codecs: InternalCodecNode[] = [];
  incomingEdges: InternalEdge[] = [];
  outgoingEdges: InternalEdge[] = [];
  // isCollapsed: boolean = false;
  // isVisible: boolean = true;

  constructor(rfid: RF_graphId, type: NodeType, graph: Graph) {
    super(rfid, type);
    this.id = graph.gNum;
    this.gType = graph.gType;
    this.gName = graph.gName;
    this.gFailureString = graph.gFailureString;
    this.gLocalParams = graph.gLocalParams;
  }

  getGraphTypeString(): string {
    switch (this.gType) {
      case ZL_GraphType.ZL_GraphType_standard:
        return 'Standard';
      case ZL_GraphType.ZL_GraphType_static:
        return 'Static';
      case ZL_GraphType.ZL_GraphType_selector:
        return 'Selector';
      case ZL_GraphType.ZL_GraphType_function:
        return 'Function';
      case ZL_GraphType.ZL_GraphType_multiInput:
        return 'Multiple_Input';
      case ZL_GraphType.ZL_GraphType_parameterized:
        return 'Parameterized';
      default:
        return 'Unknown';
    }
  }
}
