// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedRefParamInfo} from '../interfaces/SerializedRefParamInfo';

export class RefParamInfo {
  readonly paramId: number;

  constructor(paramId: number) {
    this.paramId = paramId;
  }

  static fromObject(obj: SerializedRefParamInfo): RefParamInfo {
    return new RefParamInfo(obj.paramId);
  }

  toStringDotFormat(): string {
    return `(${this.paramId})`;
  }
}
