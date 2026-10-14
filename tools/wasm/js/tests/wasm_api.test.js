// Copyright (c) Meta Platforms, Inc. and affiliates.
import {describe, it, before} from 'node:test';
import assert from 'node:assert/strict';
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';

import {createOpenZL} from '../wasm_api.js';

const wasmUrl = new URL('../openzl.wasm', import.meta.url);
const jsUrl = new URL('../openzl.js', import.meta.url);
const hasArtifact = existsSync(fileURLToPath(wasmUrl)) && existsSync(fileURLToPath(jsUrl));
const TEST_TRAIN_MAX_TIME_SECS = 5; // Mirrors native C++ test time

describe('wasm_api', () => {
  let zl;
  const trainProgressEvents = [];
  const benchmarkProgressEvents = [];
  let handleTrainingProgress = (progress, message) => trainProgressEvents.push({progress, message});
  let handleBenchmarkProgress = (progress, message) => benchmarkProgressEvents.push({progress, message});

  before(async () => {
    if (!hasArtifact) {
      throw new Error(
        'openzl.wasm/js not found next to wasm_api.js — build with: emcmake cmake -DOPENZL_BUILD_WASM=ON -B build-wasm && cmake --build build-wasm --target openzl_wasm && cp build-wasm/tools/wasm/openzl.{js,wasm} fbcode/openzl/dev/tools/wasm/js/ (or tools/wasm/js/ in OSS). Skipping would hide missing coverage, so this fails fast instead.',
      );
    }
    zl = await createOpenZL({
      onTrainingProgress: (progress, message) => handleTrainingProgress(progress, message),
      onBenchmarkProgress: (progress, message) => handleBenchmarkProgress(progress, message),
    });
  });

  it('accepts repeated initialization with the same profile table', async () => {
    const second = await createOpenZL();
    const compressor = second.getSerializedCompressor('serial');
    assert.ok(compressor.length > 0);
  });

  it('rejects a WASM table with duplicate profile names', async () => {
    await assert.rejects(createOpenZL({
      onRuntimeInitialized() {
        const profileName = this._openzl_wasm_profileName;
        this._openzl_wasm_profileName = (value) => profileName(value === 1 ? 0 : value);
      },
    }), /duplicate profile names/);
  });

  it('marks the profiles this build supports as trainable', () => {
    const supported = zl.profiles.filter(({isSupported}) => isSupported);
    assert.deepEqual(supported.map(({name}) => name).sort(), [
      'i8',
      'le-i16',
      'le-i32',
      'le-i64',
      'le-u16',
      'le-u32',
      'le-u64',
      'serial',
      'u8',
    ]);
    for (const profile of supported) {
      assert.equal(profile.isTrainable, true, profile.name);
    }
  });

  it('getSerializedCompressor returns non-empty bytes and is cached copy-on-read', () => {
    const c1 = zl.getSerializedCompressor('serial');
    assert.ok(c1 instanceof Uint8Array);
    assert.ok(c1.length > 0);
    const orig = c1.slice();
    c1[0] ^= 0xff;
    const c2 = zl.getSerializedCompressor('serial');
    assert.deepEqual(c2, orig);
    assert.notEqual(c1[0], c2[0]);
  });

  it('includes the requested compression level in the serialized compressor', () => {
    const levelOne = zl.getSerializedCompressor('serial', 1);
    const levelNine = zl.getSerializedCompressor('serial', 9);
    assert.notDeepEqual(levelOne, levelNine);
  });

  it('all supported profiles produce a compressor by name', () => {
    for (const {name} of zl.profiles.filter(({isSupported}) => isSupported)) {
      const c = zl.getSerializedCompressor(name);
      assert.ok(c.length > 0, `profile ${name} should produce compressor`);
    }
  });

  it('lists unsupported profiles and rejects compressor creation', () => {
    const unsupported = zl.profiles.filter(({isSupported}) => !isSupported);
    assert.ok(unsupported.length > 0);
    for (const profile of unsupported) {
      assert.throws(() => zl.getSerializedCompressor(profile.name), /not supported by this WASM build/);
    }
  });

  it('exposes immutable profile metadata on the existing instance', () => {
    const profiles = zl.profiles;
    assert.ok(Object.isFrozen(profiles));
    for (const profile of profiles) {
      assert.ok(Object.isFrozen(profile));
      assert.ok(profile.description.length > 0);
      if (profile.isSupported) {
        assert.ok(zl.getSerializedCompressor(profile.name).length > 0);
      }
    }
    assert.deepEqual(profiles[0], {
      name: 'serial',
      description: 'Serial data (aka raw bytes)',
      isSupported: true,
      isTrainable: true,
    });
  });

  it('rejects unknown profile', () => {
    assert.throws(() => zl.getSerializedCompressor(99), /unknown profile/);
    assert.throws(() => zl.getSerializedCompressor(-1), /unknown profile/);
    assert.throws(() => zl.getSerializedCompressor('not-a-profile'), /unknown profile not-a-profile; available: serial, /);
  });

  it('validates compression levels', () => {
    assert.doesNotThrow(() => zl.getSerializedCompressor('serial'));
    for (const level of [-1, 0, 20, 1.5, NaN, null]) {
      assert.throws(() => zl.getSerializedCompressor('serial', level), /compressionLevel/);
    }
  });

  it('roundtrips serial data', () => {
    const src = new Uint8Array(4096);
    for (let i = 0; i < src.length; i++) src[i] = 97 + (i % 10);
    const comp = zl.getSerializedCompressor('serial');
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
    const comp = zl.getSerializedCompressor('le-u32');
    const frame = zl.compress(src, comp);
    const out = zl.decompress(frame);
    assert.deepEqual(out, src);
  });

  it('roundtrips empty input', () => {
    const comp = zl.getSerializedCompressor('serial');
    const frame = zl.compress(new Uint8Array(0), comp);
    const out = zl.decompress(frame);
    assert.equal(out.length, 0);
  });

  it('validates Uint8Array inputs', () => {
    const comp = zl.getSerializedCompressor('serial');
    assert.throws(() => zl.train('not bytes', comp), /expects Uint8Array/);
    assert.throws(() => zl.train(new Uint8Array(10), 'not comp'), /expects.*compressor/);
    assert.throws(() => zl.compress('not bytes', comp), /expects Uint8Array/);
    assert.throws(() => zl.compress(new Uint8Array(10), 'not comp'), /expects.*compressor/);
    assert.throws(() => zl.decompress('bad'), /expects Uint8Array/);
    assert.throws(() => zl.benchmark('bad', comp), /expects Uint8Array/);
  });

  it('validates training inputs', () => {
    const comp = zl.getSerializedCompressor('serial');
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
    trainProgressEvents.length = 0;
    const src = new Uint8Array(1024);
    for (let i = 0; i < src.length; i++) src[i] = 97 + (i % 10);
    const base = zl.getSerializedCompressor('serial');
    // Without paretoFrontier the trainer returns exactly one compressor.
    const [trained] = zl.train(src, base, {
      maxTimeSecs: TEST_TRAIN_MAX_TIME_SECS,
    });
    assert.ok(trained.length > 0);
    assert.ok(trainProgressEvents.length > 0);
    for (const event of trainProgressEvents) {
      // Verify that we are getting progress updates when training.
      assert.equal(typeof event.message, 'string');
      assert.ok(event.progress >= 0 && event.progress <= 1);
    }
    const aceEvents = trainProgressEvents.filter(({message}) => message.startsWith('Training ACE graph'));
    assert.ok(aceEvents.length >= 2);
    assert.ok(aceEvents.every(({message}) => message === 'Training ACE graph 1 / 1: ACE progress'));
    const aceProgress = aceEvents.map(({progress}) => progress);
    for (let i = 1; i < aceProgress.length; i++) {
      assert.ok(aceProgress[i] >= aceProgress[i - 1]);
    }

    const frame = zl.compress(src, trained);
    assert.deepEqual(zl.decompress(frame), src);
  });

  it('continues training if the progress callback throws', () => {
    const src = new Uint8Array(1024);
    for (let i = 0; i < src.length; i++) src[i] = 97 + (i % 10);
    const base = zl.getSerializedCompressor('serial');
    const stderrMessages = [];
    const originalConsoleError = console.error;
    console.error = (message) => stderrMessages.push(message);
    handleTrainingProgress = () => {
      throw new Error('UI callback failed');
    };

    try {
      const trained = zl.train(src, base, {
        maxTimeSecs: TEST_TRAIN_MAX_TIME_SECS,
      });
      assert.ok(trained.length > 0);
    } finally {
      handleTrainingProgress = (progress, message) => trainProgressEvents.push({progress, message});
      console.error = originalConsoleError;
    }

    assert.ok(stderrMessages.some((message) => message.includes('UI callback failed')));
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
    const base = zl.getSerializedCompressor('serial');

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
    const comp = zl.getSerializedCompressor('serial');
    benchmarkProgressEvents.length = 0;
    const r = zl.benchmark(src, comp, 2);
    assert.deepEqual(
      benchmarkProgressEvents.map(({progress}) => progress),
      [0.25, 0.5, 0.75, 1],
    );
    assert.deepEqual(
      benchmarkProgressEvents.map(({message}) => message),
      [
        'Benchmarking compression',
        'Benchmarking compression',
        'Benchmarking decompression',
        'Benchmarking decompression',
      ],
    );
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
