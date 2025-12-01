// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {BrandedType} from './BrandedType';
export type ZL_IDType = BrandedType<number, 'ZL_IDType'>;

export type StreamID = BrandedType<number, 'StreamID'>;
export type CodecID = BrandedType<number, 'CodecID'>;
export type GraphID = BrandedType<number, 'GraphID'>;
export type ChunkID = BrandedType<number, 'ChunkID'>;

export enum ZL_Type {
  ZL_Type_serial = 1,
  ZL_Type_struct = 2,
  ZL_Type_numeric = 4,
  ZL_Type_string = 8,
}

export enum ZL_GraphType {
  ZL_GraphType_standard = 0,
  ZL_GraphType_static = 1,
  ZL_GraphType_selector = 2,
  ZL_GraphType_function = 3,
  ZL_GraphType_multiInput = 4,
  ZL_GraphType_parameterized = 5,
}
