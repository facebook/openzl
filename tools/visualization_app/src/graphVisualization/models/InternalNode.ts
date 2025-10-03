// Copyright (c) Meta Platforms, Inc. and affiliates.

import {NodeType} from './types';
import type {RF_nodeId} from './types';

export class InternalNode {
  // React Flow properties
  rfid: RF_nodeId;
  type: NodeType;
  isCollapsed = false;
  isVisible = true;
  inLargestCompressionPath = false;

  constructor(rfid: RF_nodeId, type: NodeType) {
    this.rfid = rfid;
    this.type = type;
  }
}
