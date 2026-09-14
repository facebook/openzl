// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedIntParamInfo} from './SerializedIntParamInfo';
import type {SerializedCopyParamInfo} from './SerializedCopyParamInfo';
import type {SerializedRefParamInfo} from './SerializedRefParamInfo';

export interface SerializedLocalParamInfo {
  intParams: SerializedIntParamInfo[];
  copyParams: SerializedCopyParamInfo[];
  refParams: SerializedRefParamInfo[];
}
