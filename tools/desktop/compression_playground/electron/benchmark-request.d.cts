// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {BenchmarkRequest} from './native-worker.cjs';

/**
 * Returns a validated copy of `request` with unknown keys stripped, or throws a
 * TypeError or RangeError naming the first invalid field.
 */
export function validateRequest(request: unknown): BenchmarkRequest;
