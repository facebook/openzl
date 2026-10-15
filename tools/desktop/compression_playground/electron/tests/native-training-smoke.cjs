// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {run} = require('../native-worker.cjs');

const SAMPLE_SIZE = 5_000_000;

function sampleData() {
  const sample = Buffer.alloc(SAMPLE_SIZE);
  const line = Buffer.from('OpenZL compression playground sample data\n');
  for (let offset = 0; offset < sample.length; offset += line.length) {
    line.copy(sample, offset, 0, Math.min(line.length, sample.length - offset));
  }
  return sample;
}

async function main() {
  const executable = process.platform === 'win32' ? 'zli.exe' : 'zli';
  const zliPath = path.resolve(__dirname, '..', 'native', 'build', 'Release', executable);
  const results = await run({
    zliPath,
    request: {
      input: {data: sampleData()},
      iterations: 1,
      compressors: [
        {
          id: 1,
          compressor: 'OpenZL',
          level: 1,
          profile: 'serial',
          trainRequested: true,
          candidates: 10,
          profileArg: '',
        },
        {id: 2, compressor: 'zstd', level: 5},
        {id: 3, compressor: 'gzip', level: 5},
      ],
    },
  });
  const openZLResults = results.filter((result) => result.rowId === 1);
  if (
    openZLResults.length < 1 ||
    openZLResults.length > 10 ||
    results.length !== openZLResults.length + 2 ||
    results.some((result) => result.srcSize !== SAMPLE_SIZE)
  ) {
    throw new Error('Native training returned an invalid result');
  }

  const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), 'openzl-playground-'));
  try {
    const inputPath = path.join(temporaryDirectory, 'sample input.bin');
    fs.writeFileSync(inputPath, sampleData());
    const pathResults = await run({
      zliPath,
      request: {
        input: {path: inputPath},
        iterations: 1,
        compressors: [
          {
            id: 1,
            compressor: 'OpenZL',
            level: 1,
            profile: 'serial',
            trainRequested: false,
            candidates: 10,
            profileArg: '',
          },
        ],
      },
    });
    if (pathResults.length !== 1 || pathResults[0]?.srcSize !== SAMPLE_SIZE) {
      throw new Error('Native file-path benchmark returned an invalid result');
    }

    let zliFailure;
    try {
      await run({
        zliPath,
        request: {
          input: {path: inputPath},
          iterations: 1,
          compressors: [
            {
              id: 1,
              compressor: 'OpenZL',
              level: 1,
              profile: 'not-a-profile',
              trainRequested: false,
              candidates: 10,
              profileArg: '',
            },
          ],
        },
      });
    } catch (error) {
      zliFailure = error;
    }
    if (!(zliFailure instanceof Error) || !zliFailure.message.includes("Profile not found: 'not-a-profile'")) {
      throw new Error('zli failure was not returned to the coordinator');
    }
  } finally {
    fs.rmSync(temporaryDirectory, {recursive: true, force: true});
  }

  process.stdout.write(
    `${JSON.stringify({ok: true, openZLCandidates: openZLResults.length, results: results.length, srcSize: results[0].srcSize})}\n`,
  );
}

main().catch((error) => {
  process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`);
  process.exitCode = 1;
});
