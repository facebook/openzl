// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import sample from '../src/bundledSample.json';
import {BUNDLED_SAMPLE, TRY_SAMPLE_LABEL} from '../src/sample.ts';

describe('the bundled sample', () => {
  it('is the file the shared constants name, at the size they give', () => {
    expect(BUNDLED_SAMPLE).toEqual({kind: 'bundled-sample', name: sample.name, size: sample.size});
  });

  it('is offered with its size', () => {
    expect(TRY_SAMPLE_LABEL).toBe('Try a 4.2 MB sample');
  });
});
