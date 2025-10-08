// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {ZL_IDType, StreamID} from '../models/idTypes';
import type {SerializedLocalParamInfo} from './SerializedLocalParamInfo';

export interface SerializedCodec {
  name: string;
  cType: boolean;
  cID: ZL_IDType;
  cHeaderSize: number;
  cFailureString: string;
  cLocalParams: SerializedLocalParamInfo;
  inputStreams: StreamID[];
  outputStreams: StreamID[];
}
