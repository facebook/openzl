// Copyright (c) Meta Platforms, Inc. and affiliates.

import {ZL_Type} from '../models/idTypes';

export interface SerializedStream {
  chunkId: number;
  type: ZL_Type;
  outputIdx: number;
  eltWidth: number;
  numElts: number;
  cSize: number;
  share: number;
  contentSize: number;
}
