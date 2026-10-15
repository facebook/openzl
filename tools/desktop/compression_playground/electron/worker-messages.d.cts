// Copyright (c) Meta Platforms, Inc. and affiliates.

/** Returns a validated copy of a benchmark worker message, or throws naming the invalid field. */
export function parseWorkerMessage(message: unknown): {type: string} & Record<string, unknown>;
