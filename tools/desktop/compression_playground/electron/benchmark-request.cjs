// Copyright (c) Meta Platforms, Inc. and affiliates.

'use strict';

function validateRequest(request) {
  if (request === null || typeof request !== 'object') {
    throw new TypeError('Invalid benchmark request');
  }
  if (!Number.isInteger(request.iterations) || request.iterations < 1 || request.iterations > 1000) {
    throw new RangeError('Iterations must be an integer from 1 to 1000');
  }
  if (!Array.isArray(request.compressors) || request.compressors.length === 0) {
    throw new RangeError('Choose at least one compressor');
  }
  for (const row of request.compressors) {
    if (row === null || typeof row !== 'object' || !Number.isInteger(row.id)) {
      throw new TypeError('Invalid compressor row');
    }
    if (!['OpenZL', 'zstd', 'gzip'].includes(row.compressor)) {
      throw new TypeError('Unknown compressor');
    }
    const maximumLevel = row.compressor === 'zstd' ? 19 : 9;
    if (!Number.isInteger(row.level) || row.level < 1 || row.level > maximumLevel) {
      throw new RangeError(`Invalid ${row.compressor} compression level`);
    }
    if (row.compressor === 'OpenZL') {
      if (typeof row.profile !== 'string' || row.profile.length === 0) {
        throw new TypeError('OpenZL profile is missing');
      }
      if (typeof row.trainRequested !== 'boolean') {
        throw new TypeError('OpenZL training flag is invalid');
      }
      if (!Number.isInteger(row.candidates) || row.candidates < 10 || row.candidates > 25) {
        throw new RangeError('OpenZL candidate count must be between 10 and 25');
      }
      if (typeof row.profileArg !== 'string' || row.profileArg.length > 4096) {
        throw new TypeError('OpenZL profile argument is invalid');
      }
    }
  }
  const input = request.input;
  if (input === null || typeof input !== 'object') {
    throw new TypeError('Benchmark input is missing');
  }
  if ('path' in input) {
    if (typeof input.path !== 'string' || input.path.length === 0 || input.path.includes('\0')) {
      throw new TypeError('Invalid input path');
    }
  } else if (!('data' in input) || !(input.data instanceof Uint8Array)) {
    throw new TypeError('Benchmark input must contain a file path or bytes');
  }
  return request;
}

module.exports = {validateRequest};
