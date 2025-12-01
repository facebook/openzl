// Copyright (c) Meta Platforms, Inc. and affiliates.

import {ZL_GraphType} from '../models/idTypes';
import type {SerializedLocalParamInfo} from './SerializedLocalParamInfo';

export interface SerializedGraph {
  gType: ZL_GraphType;
  gName: string;
  chunkId: number;
  gFailureString: string;
  gLocalParams: SerializedLocalParamInfo;
  codecIDs: number[];
}
