// Copyright (c) Meta Platforms, Inc. and affiliates.

import {IntParamInfo} from './IntParamInfo';
import {CopyParamInfo} from './CopyParamInfo';
import {RefParamInfo} from './RefParamInfo';
import type {SerializedLocalParamInfo} from '../interfaces/SerializedLocalParamInfo';

export class LocalParamInfo {
  readonly intParams: IntParamInfo[];
  readonly copyParams: CopyParamInfo[];
  readonly refParams: RefParamInfo[];

  constructor(intParams: IntParamInfo[], copyParams: CopyParamInfo[], refParams: RefParamInfo[]) {
    this.intParams = intParams;
    this.copyParams = copyParams;
    this.refParams = refParams;
  }

  static fromObject(obj: SerializedLocalParamInfo): LocalParamInfo {
    return new LocalParamInfo(
      obj.intParams.map((intParam) => IntParamInfo.fromObject(intParam)),
      obj.copyParams.map((copyParam) => CopyParamInfo.fromObject(copyParam)),
      obj.refParams.map((refParam) => RefParamInfo.fromObject(refParam)),
    );
  }

  // Function to check that a node should have a local params display button
  hasLocalParams() {
    return this.intParams.length > 0 || this.copyParams.length > 0 || this.refParams.length > 0;
  }

  toStringDotFormat(): string {
    const intParamsStr =
      this.intParams.length > 0
        ? `IntParams (paramId, paramValue): ${this.intParams.map((param) => param.toStringDotFormat()).join(', ')}`
        : '';
    const copyParamsStr =
      this.copyParams.length > 0
        ? `CopyParams (paramId, paramSize): ${this.copyParams.map((param) => param.toStringDotFormat()).join(', ')}`
        : '';
    const refParamsStr =
      this.refParams.length > 0
        ? `RefParams (paramId): ${this.refParams.map((param) => param.toStringDotFormat()).join(', ')}`
        : '';
    // Removes extra spaces and newlines that are not defined within the string.
    // Used for code clarity purposes within a code editor, so it isn't one large
    // string spanning beyond screen width.
    return `${intParamsStr}\\n${copyParamsStr}\\n${refParamsStr}`;
  }
}
