// Copyright (c) Meta Platforms, Inc. and affiliates.
import {describe, it, before} from 'node:test';
import assert from 'node:assert/strict';
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';

import {createOpenZL, Profile} from '../wasm_api.js';

const wasmUrl = new URL('../openzl.wasm', import.meta.url);
const jsUrl = new URL('../openzl.js', import.meta.url);
const hasArtifact = existsSync(fileURLToPath(wasmUrl)) && existsSync(fileURLToPath(jsUrl));
const TEST_TRAIN_MAX_TIME_SECS = 5; // Mirrors native C++ test time

describe('Profile', () => {
  it('is defined and frozen when the module loads', () => {
    assert.ok(Object.isFrozen(Profile));
    assert.equal(Profile.SERIAL, 0);
    assert.equal(Profile.U8, 1);
    assert.equal(Profile.I8, 2);
    assert.equal(Profile.U16, 3);
    assert.equal(Profile.I16, 4);
    assert.equal(Profile.U32, 5);
    assert.equal(Profile.I32, 6);
    assert.equal(Profile.U64, 7);
    assert.equal(Profile.I64, 8);
  });
});

describe('wasm_api', () => {
  let zl;

  before(async () => {
    if (!hasArtifact) {
      throw new Error(
        'openzl.wasm/js not found next to wasm_api.js — build with: emcmake cmake -DOPENZL_BUILD_WASM=ON -B build-wasm && cmake --build build-wasm --target openzl_wasm && cp build-wasm/tools/wasm/openzl.{js,wasm} fbcode/openzl/dev/tools/wasm/js/ (or tools/wasm/js/ in OSS). Skipping would hide missing coverage, so this fails fast instead.',
      );
    }
    zl = await createOpenZL();
  });

  it('accepts repeated initialization with the same profile table', async () => {
    const second = await createOpenZL();
    const compressor = second.getSerializedCompressor(Profile.SERIAL);
    assert.ok(compressor.length > 0);
  });

  it('getSerializedCompressor returns non-empty bytes and is cached copy-on-read', () => {
    const c1 = zl.getSerializedCompressor(Profile.SERIAL);
    assert.ok(c1 instanceof Uint8Array);
    assert.ok(c1.length > 0);
    const orig = c1.slice();
    c1[0] ^= 0xff;
    const c2 = zl.getSerializedCompressor(Profile.SERIAL);
    assert.deepEqual(c2, orig);
    assert.notEqual(c1[0], c2[0]);
  });

  it('all profiles produce a compressor', () => {
    for (const p of Object.values(Profile)) {
      const c = zl.getSerializedCompressor(p);
      assert.ok(c.length > 0, `profile ${p} should produce compressor`);
    }
  });

  it('rejects unknown profile', () => {
    assert.throws(() => zl.getSerializedCompressor(99), /unknown profile/);
    assert.throws(() => zl.getSerializedCompressor(-1), /unknown profile/);
  });

  it('roundtrips serial data', () => {
    const src = new Uint8Array(4096);
    for (let i = 0; i < src.length; i++) src[i] = 97 + (i % 10);
    const comp = zl.getSerializedCompressor(Profile.SERIAL);
    const frame = zl.compress(src, comp);
    assert.ok(frame.length > 0);
    const out = zl.decompress(frame);
    assert.deepEqual(out, src);
  });

  it('roundtrips u32 little-endian ints', () => {
    const count = 1024;
    const src = new Uint8Array(count * 4);
    const view = new DataView(src.buffer);
    for (let i = 0; i < count; i++) view.setUint32(i * 4, i * 12345, true);
    const comp = zl.getSerializedCompressor(Profile.U32);
    const frame = zl.compress(src, comp);
    const out = zl.decompress(frame);
    assert.deepEqual(out, src);
  });

  it('roundtrips empty input', () => {
    const comp = zl.getSerializedCompressor(Profile.SERIAL);
    const frame = zl.compress(new Uint8Array(0), comp);
    const out = zl.decompress(frame);
    assert.equal(out.length, 0);
  });

  it('validates Uint8Array inputs', () => {
    const comp = zl.getSerializedCompressor(Profile.SERIAL);
    assert.throws(() => zl.train('not bytes', comp), /expects Uint8Array/);
    assert.throws(() => zl.train(new Uint8Array(10), 'not comp'), /expects.*compressor/);
    assert.throws(() => zl.compress('not bytes', comp), /expects Uint8Array/);
    assert.throws(() => zl.compress(new Uint8Array(10), 'not comp'), /expects.*compressor/);
    assert.throws(() => zl.decompress('bad'), /expects Uint8Array/);
    assert.throws(() => zl.benchmark('bad', comp), /expects Uint8Array/);
  });

  it('validates training inputs', () => {
    const comp = zl.getSerializedCompressor(Profile.SERIAL);
    assert.throws(() => zl.train(new Uint8Array(0), comp), /non-empty data/);
    assert.throws(() => zl.train(new Uint8Array(10), new Uint8Array(0)), /non-empty serialized compressor/);

    const oversized = new Proxy(new Uint8Array(1), {
      get(target, property) {
        return property === 'length' ? 150 * 1024 * 1024 : Reflect.get(target, property);
      },
    });
    assert.throws(() => zl.train(oversized, comp), /smaller than 150 MiB/);

    // Train options are coerced to non-negative integers for the u64 ABI, so
    // non-finite or non-numeric values throw a clear error naming the field.
    for (const bad of [NaN, Infinity, '4', true]) {
      assert.throws(() => zl.train(new Uint8Array(10), comp, {threads: bad}), /threads/);
      assert.throws(() => zl.train(new Uint8Array(10), comp, {maxTimeSecs: bad}), /maxTimeSecs/);
      assert.throws(
        () =>
          zl.train(new Uint8Array(10), comp, {
            paretoFrontier: true,
            maxNumCandidates: bad,
          }),
        /maxNumCandidates/,
      );
    }

    assert.throws(
      () =>
        zl.train(new Uint8Array(10), comp, {
          paretoFrontier: true,
          maxNumCandidates: 26,
        }),
      /maxNumCandidates must be at most 25/,
    );
  });

  it('trains a compressor that can roundtrip data', () => {
    const src = new Uint8Array(1024);
    for (let i = 0; i < src.length; i++) src[i] = 97 + (i % 10);
    const base = zl.getSerializedCompressor(Profile.SERIAL);
    // Without paretoFrontier the trainer returns exactly one compressor.
    const [trained] = zl.train(src, base, {
      maxTimeSecs: TEST_TRAIN_MAX_TIME_SECS,
    });
    assert.ok(trained.length > 0);

    const frame = zl.compress(src, trained);
    assert.deepEqual(zl.decompress(frame), src);
  });

  it('reports native training errors', () => {
    assert.throws(
      () => zl.train(new Uint8Array(10), new Uint8Array([1, 2, 3])),
      (error) => Number.isInteger(error.code),
    );
  });

  it('paretoFrontier returns a frontier of usable compressors', () => {
    const src = new Uint8Array(1024);
    for (let i = 0; i < src.length; i++) src[i] = 97 + (i % 10);
    const base = zl.getSerializedCompressor(Profile.SERIAL);

    const frontier = zl.train(src, base, {
      maxTimeSecs: TEST_TRAIN_MAX_TIME_SECS,
      paretoFrontier: true,
      maxNumCandidates: 25,
    });
    assert.ok(Array.isArray(frontier));
    // The frontier's size depends on the data, so the only guarantees are that
    // training produced something and stayed within the candidate limit.
    assert.ok(frontier.length >= 1);
    assert.ok(frontier.length <= 25);

    // Every compressor on the frontier is usable, not just the first. Sizes are
    // not asserted: the speed-optimal end can emit more bytes than it was given.
    for (const compressor of frontier) {
      assert.ok(compressor instanceof Uint8Array);
      assert.ok(compressor.length > 0);
      assert.deepEqual(zl.decompress(zl.compress(src, compressor)), src);
    }
  });

  it('benchmark compresses and decompresses data and returns expected metrics', () => {
    const src = new Uint8Array(2048);
    for (let i = 0; i < src.length; i++) src[i] = i & 0xff;
    const comp = zl.getSerializedCompressor(Profile.SERIAL);
    const r = zl.benchmark(src, comp, 2);
    assert.equal(r.iterations, 2);
    assert.equal(r.srcSize, src.length);
    assert.ok(r.compressedSize > 0);
    assert.ok(r.ratio >= 0);
    assert.ok(r.compressMs >= 0);
    assert.ok(r.decompressMs >= 0);
    assert.ok(r.compressMBps > 0);
    assert.ok(r.decompressMBps > 0);
    assert.ok(Number.isFinite(r.compressMBps) || r.compressMBps === Infinity);
    assert.ok(Number.isFinite(r.decompressMBps) || r.decompressMBps === Infinity);
    assert.ok(Number.isInteger(zl.maxBenchmarkIterations));
    assert.ok(zl.maxBenchmarkIterations >= 1);
    assert.equal(zl.benchmark(src, comp, 0).iterations, 1);
    assert.equal(zl.benchmark(src, comp, zl.maxBenchmarkIterations + 1).iterations, zl.maxBenchmarkIterations);
    assert.equal(zl.benchmark(src, comp, 1.9).iterations, 1);
  });
});
