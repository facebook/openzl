// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedCopyParamInfo} from '../interfaces/SerializedCopyParamInfo';

export class CopyParamInfo {
  readonly paramId: number;
  readonly paramSize: number;
  readonly paramData: Uint8Array;

  constructor(paramID: number, paramSize: number, paramData: Uint8Array) {
    this.paramId = paramID;
    this.paramSize = paramSize;
    this.paramData = paramData;
  }

  static fromObject(copyParamInfo: SerializedCopyParamInfo): CopyParamInfo {
    return new CopyParamInfo(copyParamInfo.paramId, copyParamInfo.paramSize, copyParamInfo.paramData);
  }

  toStringDotFormat(): string {
    return `(${this.paramId}, ${this.paramSize}, {${this.paramData}})`;
  }
}
