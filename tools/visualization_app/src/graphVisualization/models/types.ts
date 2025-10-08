// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {BrandedType, BrandedChildType} from '../../models/BrandedType';

export enum NodeType {
  Codec = 'codec',
  Graph = 'graph',
  Edge = 'edge',
}

// React Flow's Node implementation requires ids of nodes to be a string
export type RF_nodeId = BrandedType<string, 'RF_nodeId'>;
export type RF_edgeId = BrandedType<string, 'RF_edgeId'>;
export type RF_codecId = BrandedChildType<RF_nodeId, 'RF_codecId'>;
export type RF_graphId = BrandedChildType<RF_nodeId, 'RF_graphId'>;
