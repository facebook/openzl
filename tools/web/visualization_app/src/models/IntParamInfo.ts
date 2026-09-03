// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedIntParamInfo} from '../interfaces/SerializedIntParamInfo';

export class IntParamInfo {
  readonly paramId: number;
  readonly paramValue: number;

  constructor(paramId: number, paramValue: number) {
    this.paramId = paramId;
    this.paramValue = paramValue;
  }

  static fromObject(obj: SerializedIntParamInfo): IntParamInfo {
    return new IntParamInfo(obj.paramId, obj.paramValue);
  }

  toStringDotFormat(): string {
    return `(${this.paramId}, ${this.paramValue})`;
  }
}
