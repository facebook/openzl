// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {Stream} from '../../models/Stream';
import type {InternalNode} from './InternalNode';

export class InternalEdge {
  id: string;
  stream: Stream;
  source: InternalNode;
  target: InternalNode;
  label: string;
  hidden = false;
  inLargestCompressionPath = false;

  constructor(id: string, stream: Stream, source: InternalNode, target: InternalNode, label: string) {
    this.id = id;
    this.stream = stream;
    this.source = source;
    this.target = target;
    this.label = label;
  }
}
