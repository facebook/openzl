// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const {z} = require('zod');

const count = z.int().min(0);
const duration = z.number().min(0);
// Throughput is Infinity when an operation measured 0 ms.
const throughput = z.number().min(0).or(z.literal(Infinity));

const resultSchema = z.object({
  rowId: z.int(),
  label: z.string(),
  candidate: count,
  iterations: z.int().min(1),
  srcSize: count,
  compressedSize: count,
  ratio: z.number().min(0),
  compressMs: duration,
  decompressMs: duration,
  compressMBps: throughput,
  decompressMBps: throughput,
});

const row = {rowId: z.int(), rowIndex: count, rowCount: z.int().min(1), label: z.string()};

const progressSchema = z.discriminatedUnion('stage', [
  z.object({...row, stage: z.literal('benchmark')}),
  z.object({...row, stage: z.literal('train')}),
  z.object({...row, stage: z.literal('candidates'), candidate: z.int().min(1), candidateCount: z.int().min(1)}),
]);

const workerMessageSchema = z.discriminatedUnion('type', [
  z.object({type: z.literal('child'), pid: z.int().min(1), state: z.enum(['spawned', 'exited'])}),
  z.object({type: z.literal('progress'), progress: progressSchema}),
  z.object({type: z.literal('partial'), results: z.array(resultSchema)}),
  z.object({type: z.literal('results'), results: z.array(resultSchema)}),
  z.object({type: z.literal('error'), message: z.string()}),
  z.object({type: z.literal('cancelled')}),
]);

/**
 * Returns a validated copy of a message from the benchmark worker, or throws
 * naming the first invalid field. Only validated copies reach the renderer.
 */
function parseWorkerMessage(message) {
  const parsed = workerMessageSchema.safeParse(message);
  if (parsed.success) {
    return parsed.data;
  }
  const [issue] = parsed.error.issues;
  const field = issue.path.length > 0 ? `${issue.path.join('.')}: ` : '';
  throw new Error(`Native benchmark process sent an invalid message (${field}${issue.message})`);
}

module.exports = {parseWorkerMessage};
