// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const {z} = require('zod');
// Shared with the page, so the limits checked here are the ones it offers.
const {webConstants} = require('./web-constants.cjs');
const {maxLevel, trainedCandidates} = webConstants('compressorLimits.json');

const ITERATIONS = 'Iterations must be an integer from 1 to 1000';
const NO_COMPRESSORS = 'Choose at least one compressor';
const CANDIDATES = `OpenZL candidate count must be between ${trainedCandidates.desktopMin} and ${trainedCandidates.max}`;
const INVALID_ROW = 'Invalid compressor row';
const NO_INPUT = 'Benchmark input must be a file path or the bundled sample';
const INVALID_PATH = 'Invalid input path';
const INVALID_PROFILE_ARG = 'OpenZL profile argument is invalid';

function levelSchema(compressor, maximum) {
  const message = `Invalid ${compressor} compression level`;
  return z.int({error: message}).min(1, message).max(maximum, message);
}

// Out-of-range values throw RangeError; everything else throws TypeError.
const RANGE_MESSAGES = new Set([
  ITERATIONS,
  NO_COMPRESSORS,
  CANDIDATES,
  'Invalid OpenZL compression level',
  'Invalid zstd compression level',
  'Invalid gzip compression level',
]);

// The row's shape is checked first, so a malformed row is not reported as an
// unknown compressor.
const compressorSchema = z
  .object({id: z.int({error: INVALID_ROW})}, {error: INVALID_ROW})
  .loose()
  .pipe(
    z.discriminatedUnion(
      'compressor',
      [
        z
          .object({
            id: z.int(),
            compressor: z.literal('OpenZL'),
            level: levelSchema('OpenZL', maxLevel.OpenZL),
            profile: z.string({error: 'OpenZL profile is missing'}).min(1, 'OpenZL profile is missing'),
            trainRequested: z.boolean({error: 'OpenZL training flag is invalid'}),
            // Only training reads it.
            candidates: z
              .int({error: CANDIDATES})
              .min(trainedCandidates.desktopMin, CANDIDATES)
              .max(trainedCandidates.max, CANDIDATES)
              .optional(),
            profileArg: z.string({error: INVALID_PROFILE_ARG}).max(4096, INVALID_PROFILE_ARG),
          })
          .refine((row) => !row.trainRequested || row.candidates !== undefined, {
            message: CANDIDATES,
            path: ['candidates'],
          }),
        z.object({id: z.int(), compressor: z.literal('zstd'), level: levelSchema('zstd', maxLevel.zstd)}),
        z.object({id: z.int(), compressor: z.literal('gzip'), level: levelSchema('gzip', maxLevel.gzip)}),
      ],
      {error: 'Unknown compressor'},
    ),
  );

const inputSchema = z
  .object(
    {
      path: z
        .string({error: INVALID_PATH})
        .min(1, INVALID_PATH)
        .refine((path) => !path.includes('\0'), INVALID_PATH)
        .optional(),
      // The main process swaps in where the sample is installed, so the page
      // never has to know.
      sample: z.literal(true, {error: NO_INPUT}).optional(),
    },
    {error: 'Benchmark input is missing'},
  )
  .refine((input) => (input.path === undefined) !== (input.sample === undefined), NO_INPUT);

/** Keys are in the order they are reported: the first invalid one names the error. */
const benchmarkRequestSchema = z.object(
  {
    iterations: z.int({error: ITERATIONS}).min(1, ITERATIONS).max(1000, ITERATIONS),
    compressors: z.array(compressorSchema, {error: NO_COMPRESSORS}).min(1, NO_COMPRESSORS),
    input: inputSchema,
  },
  {error: 'Invalid benchmark request'},
);

/**
 * Returns a validated copy of the request with unknown keys removed, so only
 * known fields reach the worker. Throws a TypeError or RangeError naming the
 * first invalid field.
 */
function validateRequest(request) {
  const parsed = benchmarkRequestSchema.safeParse(request);
  if (parsed.success) {
    return parsed.data;
  }
  const {message} = parsed.error.issues[0];
  throw RANGE_MESSAGES.has(message) ? new RangeError(message) : new TypeError(message);
}

module.exports = {validateRequest};
