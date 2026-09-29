# PivCo-Huffman GPU Development Log

This log tracks decode and encode optimization ideas for the dataset benchmark.
Worked optimizations are committed separately. Failed ideas are rolled back and
kept here with the measured reason.

Benchmark protocol unless otherwise noted:

- Build mode: `@fbcode//mode/opt`
- Benchmark target: `fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_bench`
- Dataset size: 268,435,456 bytes
- Block size: 32,768 bytes
- Throughput: GiB/s over decompressed bytes, measured with CUDA events; bulk
  H2D/D2H transfer is excluded.

## Plan

1. Establish real-dataset baselines from `BENCHMARKS.md` and targeted reruns.
2. Add narrow generic-shape decode fast paths when the tree shape is common in
   the real benchmark set.
3. Move from shape-specific paths to a general per-symbol parallel decoder:
   parse block metadata once, build rank/select metadata for internal-node
   bitmaps, and let CUDA threads decode output bytes independently.
4. Keep successful decode optimizations as separate commits. Roll back failed
   code and record why it did not survive.
5. Defer encode optimization until decode ideas are exhausted.

## Ideas And Results

### Flat-root decode fast path

- Status: worked, committed separately
- Idea: when the whole Huffman tree is one flat leaf, decode each output symbol
  independently in a CTA instead of using the serial generic tree-walk decoder.
- Expected impact: improves `gzip_random.gz` and the `uniform`/`flat_M*`
  synthetic cases; it should not affect broad non-flat real datasets.
- Baseline for `gzip_random.gz`, 256 MiB, 5 iterations:
  - Decode median: 2.2128 GiB/s
  - Decode best: 2.2129 GiB/s
- Result for `gzip_random.gz`, 256 MiB, 5 iterations:
  - Decode median: 261.0257 GiB/s
  - Decode best: 261.1479 GiB/s
- Notes: this only handles whole-tree flat leaves. It does not improve deep or
  mixed real datasets such as `cat-wiki.html`, `json_api.json`, or
  `chinese_text.txt`.

### Root-constant subtree merge fast path

- Status: failed, rolled back
- Idea: when the root has exactly one constant child, keep the existing serial
  decode for the non-constant child but parallelize the root bitmap merge across
  a CTA. This targets skewed datasets where the root bitmap covers every output
  byte and dominates the generic serial merge.
- Expected impact: possible gains for `calgary_pic`, `dna_fasta.fa`,
  `csv_numeric.csv`, and other skewed real files if their root split isolates a
  constant symbol.
- Result: regressed the likely target datasets, so the code was removed.
  - `calgary_pic`: 6.6559 GiB/s baseline to 3.7762 GiB/s
  - `dna_fasta.fa`: 5.4315 GiB/s baseline to 2.4459 GiB/s
  - `csv_numeric.csv`: 3.9410 GiB/s baseline to 2.1666 GiB/s
- Reason: the child subtree still decodes serially, and the extra CTA-wide
  prefix scan plus synchronization costs more than it saves for these trees.
- Caveat: the three measurements above were collected during concurrent GPU
  benchmark runs, so they are not used as final benchmark data. The
  implementation stayed rolled back because the generic rank/select decoder
  superseded it and produced broader sequential-run wins.

### Generic rank/select decoder

- Status: worked, committed separately
- Idea: parse each block into per-node bitmap/leaf metadata, build per-node
  64-bit popcount directories, then let CTA threads decode output bytes
  independently by traversing the fixed Huffman tree with rank/select.
- Expected impact: broad non-flat datasets should improve because output merge
  work moves from one serial thread per block to many threads per block.
- Results from sequential 256 MiB, 5-iteration real-dataset runs:
  - `cat-image.jpg`: 1.9884 to 7.4654 GiB/s
  - `cat-wiki.html`: 2.3813 to 6.1704 GiB/s
  - `calgary_pic`: 6.6559 to 11.8104 GiB/s
  - `dna_fasta.fa`: 5.4315 to 11.5975 GiB/s
  - `json_api.json`: 2.5705 to 6.5415 GiB/s
  - `pride.txt`: 2.8095 to 6.3873 GiB/s
  - `chinese_text.txt`: 2.4370 to 6.4994 GiB/s
  - `csv_numeric.csv`: 3.9410 to 7.6437 GiB/s
  - `source_c.c`: 2.8235 to 6.4926 GiB/s
  - `log_apache.log`: 2.3944 to 6.7020 GiB/s
  - `gzip_random.gz`: flat-root path still selected, 265.1901 GiB/s
- Notes: the parser and directory builder are still serial within each block,
  and every output byte performs a full tree traversal. This is a useful generic
  baseline but not close to the 100 GiB/s target for mixed trees.

### Rank/select 32-bit directory

- Status: worked, committed separately
- Idea: replace the rank/select directory stride from 64 bitmap bits with
  32 bitmap bits and store prefix counts as `uint16_t`. For the 32 KiB benchmark
  block size, counts fit in 16 bits, directory byte size stays about the same,
  and each output traversal step popcounts at most four bitmap bytes instead of
  eight.
- Expected impact: improve mixed-tree datasets where per-symbol rank/select
  traversal dominates over serial block parsing.
- Results from sequential 256 MiB, 5-iteration real-dataset runs:
  - `cat-image.jpg`: 7.4654 to 8.0096 GiB/s
  - `calgary_pic`: 11.8104 to 12.6213 GiB/s
  - `json_api.json`: 6.5415 to 7.2178 GiB/s

### Rank/select binary-constant node shortcut

- Status: failed, rolled back
- Idea: when an internal node has two constant children, decode its output
  symbol directly from the node bitmap. The rank/select decoder does not need a
  popcount directory or child-node traversal for that node.
- Expected impact: reduce parse metadata and per-symbol traversal work near the
  bottom of mixed trees.
- Result: regressed sampled real datasets, so the code was removed.
  - `cat-image.jpg`: 8.0096 to 7.1788 GiB/s
  - `calgary_pic`: 12.6213 to 12.3421 GiB/s
- Reason: the extra branch and node kind did not pay for itself in the hot
  traversal loop. Keeping one uniform internal-node path is faster.

### Rank/select shared node metadata

- Status: worked, committed separately
- Idea: store parsed per-node metadata in CTA shared memory instead of per-block
  global workspace. Directory data stays in global workspace, but every output
  thread repeatedly reads node records during traversal, so shared metadata may
  reduce traversal latency.
- Expected impact: improve mixed-tree datasets if node metadata load latency is
  significant compared with rank directory and bitmap loads.
- Results from sequential 256 MiB, 5-iteration real-dataset runs:
  - `cat-image.jpg`: 8.0096 to 7.8950 GiB/s
  - `calgary_pic`: 12.6213 to 12.9308 GiB/s
  - `json_api.json`: 7.2178 to 7.4054 GiB/s
  - `dna_fasta.fa`: 11.5975 to 13.1476 GiB/s
  - `cat-wiki.html`: 6.1704 to 7.6401 GiB/s
- Notes: `cat-image.jpg` regressed slightly, but the sampled real mix improved
  overall. Keeping this while continuing to look for a high-entropy-specific
  improvement.

### Top-down bulk position-list decoder

- Status: worked, committed separately
- Idea: parse the block once, then process node streams level-by-level with
  `uint16_t` position lists. Internal nodes partition positions into child
  streams; leaves write symbols directly to output. This avoids an independent
  root-to-leaf rank/select traversal for every output byte.
- Expected impact: a larger algorithmic step toward 100 GiB/s if bulk
  partitioning has better memory behavior than per-symbol rank/select traversal.
- Results from sequential 256 MiB, 5-iteration real-dataset runs:
  - `cat-image.jpg`: 7.8950 to 10.5538 GiB/s
  - `calgary_pic`: 12.9308 to 18.0469 GiB/s
  - `json_api.json`: 7.4054 to 8.4420 GiB/s
  - `cat-wiki.html`: 7.6401 to 9.0523 GiB/s
  - `dna_fasta.fa`: 13.1476 to 13.2929 GiB/s
  - `csv_numeric.csv`: 7.6437 to 11.7893 GiB/s
  - `log_apache.log`: 6.7020 to 7.4752 GiB/s
  - `source_c.c`: 6.4926 to 7.7788 GiB/s
  - `pride.txt`: 6.3873 to 8.6228 GiB/s
  - `chinese_text.txt`: 6.4994 to 9.3560 GiB/s
- Notes: this is a better algorithmic direction than independent per-symbol
  rank/select, but it is still position-list heavy and remains far below the
  100 GiB/s target for mixed real datasets.

### Bottom-up compact-stream decoder

- Status: worked, pending commit
- Idea: follow the CPU generic decoder and AVX512 merge shape more closely.
  Decode leaves into compact byte streams, then merge child streams upward using
  the parent bitmap and per-node popcount directories. The root merge writes
  directly to the final output buffer.
- Expected impact: reduce intermediate traffic versus top-down position lists
  because compact symbol streams use one byte per element instead of two-byte
  output positions, and make internal-node work resemble the CPU masked-expand
  merge.
- Initial result before chunked merges:
  - `cat-image.jpg`: 10.5538 to 10.6510 GiB/s
  - `calgary_pic`: 18.0469 to 15.3086 GiB/s
  - `json_api.json`: 8.4420 to 7.0627 GiB/s
  - `cat-wiki.html`: 9.0523 to 9.1351 GiB/s
  - `dna_fasta.fa`: 13.2929 to 16.3082 GiB/s
- Notes: the plain bottom-up path was mixed, but the user explicitly asked to
  keep exploring bottom-up rather than rolling it back.

### Bottom-up constant-child direct merge

- Status: failed, rolled back
- Idea: do not materialize constant leaves into scratch streams. Instead, have
  parent internal-node merges substitute the constant symbol directly for a
  constant child, similar to the CPU `mergeConstantVector` and
  `mergeVectorConstant` variants.
- Expected impact: reduce scratch writes and reads for trees with constant
  leaves near the bottom.
- Result:
  - `dna_fasta.fa`: 16.3082 to 13.7212 GiB/s
- Reason: the extra child-kind branches in the internal merge loop outweighed
  the avoided scratch traffic on the sampled skewed dataset.

### Bottom-up chunked bitmap-byte merge

- Status: worked, pending commit
- Idea: for large internal-node streams, have one CUDA thread handle an 8-symbol
  bitmap byte. The thread computes the child cursors once with the rank
  directory, then emits the eight parent symbols from compact child streams.
  Keep per-symbol merging for node streams smaller than 1024 symbols to preserve
  parallelism in small/deep subtrees.
- Expected impact: reduce rank-directory lookups and per-symbol branch overhead
  for large internal nodes while avoiding under-parallelizing small nodes.
- Results from sequential 256 MiB, 5-iteration real-dataset runs compared with
  the top-down bulk decoder snapshot:
  - `cat-image.jpg`: 10.5538 to 12.7302 GiB/s
  - `calgary_pic`: 18.0469 to 17.8374 GiB/s
  - `cat-wiki.html`: 9.0523 to 10.8595 GiB/s
  - `chinese_text.txt`: 9.3560 to 9.1616 GiB/s
  - `csv_numeric.csv`: 11.7893 to 13.4258 GiB/s
  - `dna_fasta.fa`: 13.2929 to 19.3354 GiB/s
  - `json_api.json`: 8.4420 to 10.2090 GiB/s
  - `log_apache.log`: 7.4752 to 8.9345 GiB/s
  - `pride.txt`: 8.6228 to 8.5871 GiB/s
  - `source_c.c`: 7.7788 to 9.2200 GiB/s
- Notes: this is not uniformly better on every real file, but it improves most
  of the broad-tree dataset set and strongly improves `dna_fasta.fa`,
  `cat-image.jpg`, and structured text. It remains far below 100 GiB/s.

### Bottom-up chunk threshold sweep

- Status: stopped, reverted to 1024
- Idea: tune the node-count threshold that selects per-symbol versus bitmap-byte
  chunked internal-node merges. A 2048-symbol threshold was prepared for a
  quick sweep.
- Result: the sweep was intentionally stopped by the user before collecting
  valid measurements. The code was restored to the last measured threshold of
  1024.

### Node-scheduled bottom-up decoder

- Status: worked, selected for eligible generic decode
- Idea: build a static preorder tree schedule once in `PivCoGpuContext`, parse
  every block against that schedule into per-block/per-node state, then execute
  bottom-up kernels across all blocks with grid work units of `(logical node,
  PivCo block)`. Constant leaves are not materialized; parent merge kernels
  inject constants directly. Flat leaves use specialized depth 1..8 kernels,
  with depth 8 as direct byte lookup.
- Expected impact: reduce per-block serial work and expose parallelism across
  the tree's node streams, especially when small/deep node streams limited the
  old one-CTA-per-block bottom-up decoder.
- Correctness:
  - `buck test --local-only @fbcode//mode/dev-nosan fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_test`

### Windowed flat bit extraction

- Status: failed, rolled back
- Idea: use the useful low-level bit-reader idea from commit `2bc254781d`.
  Replace the hot `dGetBits` bit-by-bit loop for fields up to 8 bits with a
  bounded one-or-two-byte load, shift, and mask. This keeps the existing
  one-thread-per-output flat kernels and avoids the store under-parallelism that
  made the earlier depth-3 chunked flat kernel regress.
- Build and lint:
  - `arc f contrib/pivco-huffman/gpu/pivco_gpu.cu contrib/pivco-huffman/gpu/DEVELOPMENT_LOG.md`
  - `arc lint contrib/pivco-huffman/gpu/pivco_gpu.cu contrib/pivco-huffman/gpu/DEVELOPMENT_LOG.md`
  - `buck build --show-output @fbcode//mode/opt fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_bench`
- A/B setup: NVIDIA A100, 256 MiB expanded input, 32 KiB blocks,
  20 iterations. Baseline is the existing bit-by-bit `dGetBits`; candidate is
  the bounded one-or-two-byte reader.
- Representative real files:
  - `cat-image.jpg`: 33.5220 to 44.4539 GiB/s
  - `dna_fasta.fa`: 46.6630 to 46.6719 GiB/s
  - `json_api.json`: 32.3665 to 32.6391 GiB/s
  - `cat-wiki.html`: 34.2750 to 41.7549 GiB/s
  - `calgary_pic`: 51.8455 to 51.8786 GiB/s
  - `log_apache.log`: 33.7164 to 34.1982 GiB/s
  - Geometric mean: 38.0471 to 41.3746 GiB/s (+8.7%).
- All real files:
  - `cat-image.jpg`: 33.5220 to 44.4539 GiB/s
  - `calgary_pic`: 51.8455 to 51.8786 GiB/s
  - `cat-wiki.html`: 34.2750 to 41.7549 GiB/s
  - `chinese_text.txt`: 36.3088 to 37.3933 GiB/s
  - `csv_numeric.csv`: 36.8625 to 37.0078 GiB/s
  - `dna_fasta.fa`: 46.6630 to 46.6719 GiB/s
  - `gzip_random.gz`: 246.3579 to 246.8561 GiB/s
  - `json_api.json`: 32.3665 to 32.6391 GiB/s
  - `log_apache.log`: 33.7164 to 34.1982 GiB/s
  - `pride.txt`: 40.4541 to 35.1484 GiB/s
  - `source_c.c`: 27.4840 to 27.1932 GiB/s
  - Geometric mean: 43.7077 to 45.2736 GiB/s (+3.6%).
- Conclusion: despite large wins on `cat-image.jpg` and `cat-wiki.html`, this
  misses the representative +10% gate, only improves all-real geomean by 3.6%,
  and regresses `pride.txt` by 13.1%. Because the current direction must keep a
  single generic strategy rather than a selector, the change is rolled back.

### Regular vector/vector packed stores

- Status: failed, rolled back
- Idea: after packed final stores made the fused root kernel much faster, try to
  get the same win in `scheduledMergeVectorVectorKernel`. Two variants were
  tested:
  - Keep the warp-per-32-bit mapping and have each 8-lane subgroup cooperatively
    pack eight produced symbols into one 64-bit store.
  - Align scheduled intermediate stream buffers and node stream bases, then
    replace the large-node warp mapping with a one-thread-per-bitmap-byte loop
    that writes one packed 64-bit store per eight symbols.
- Results on `json_api.json`, 256 MiB:
  - Cooperative packed stores without aligned stream bases: decode stayed flat
    at 38.9 GiB/s, and Nsight Compute still reported about 16.6 useful global
    store bytes per 32-byte sector for the profiled
    `scheduledMergeVectorVectorKernel` launch.
  - Cooperative packed stores with aligned scheduled streams: the same NCU
    launch was 1.15 ms, still about 16.2 useful global store bytes per sector,
    versus the committed warp-mapped launch's 1.13 ms and 16.6 useful store
    bytes per sector.
  - Byte-loop packed stores with aligned scheduled streams regressed the
    20-iteration `json_api.json` benchmark to 34.5173 GiB/s, versus the recent
    committed range of about 37.4-39.0 GiB/s.
- Reason: sparse-lane cooperative 64-bit stores did not improve the profiler's
  store transaction shape, and the byte-loop variant gave up the warp mapping
  that was responsible for the earlier vector/vector speedup. Regular
  vector/vector remains a gather/rank dependency problem more than a simple
  output-store packing problem.

### Scheduled producer kernel rewrites

- Status: failed, rolled back
- Profiling setup: Nsight Systems and Nsight Compute on `json_api.json`,
  256 MiB, 32 KiB blocks. The hot producer kernels before changes were
  `scheduledMergeConstantVectorKernel` at 3.746 ms total over four decodes and
  `scheduledFlatKernel<3>` at 2.533 ms total over four decodes.
- Constant/vector idea: remap large `scheduledMergeConstantVectorKernel` and
  `scheduledMergeVectorConstantKernel` nodes to the same one-warp-per-32-bits
  layout used by regular vector/vector merges.
  - Local NCU result on the largest constant/vector launch looked promising:
    duration fell from 500.38 us to 457.18 us, executed instructions fell from
    174.2M to 156.5M, and branch efficiency improved from 40.0% to 85.7%.
  - Whole-decode result did not hold. Representative 20-iteration results
    included `json_api.json` falling to 32.5912 GiB/s and `log_apache.log`
    falling to 33.5128 GiB/s. Isolated reruns confirmed `json_api.json` around
    32.61 GiB/s and `log_apache.log` around 33.7 GiB/s, both below the current
    packed-root baseline.
  - Nsight Systems after the change showed constant/vector total time dropping
    from 3.746 ms to 3.477 ms over four decodes, but regular
    vector/vector time rose from 7.038 ms to 9.374 ms. A single-store variant
    for the warp-mapped constant/vector kernel still measured only
    32.6478 GiB/s on `json_api.json` and 34.2990 GiB/s on `log_apache.log`.
- Flat-depth idea: specialize `scheduledFlatKernel<3>` so one thread decodes
  eight 3-bit symbols from three input bytes and optionally emits one 64-bit
  packed store.
  - NCU on the large `scheduledFlatKernel<3>` launch regressed from 327.84 us
    to 451.49 us. Executed instructions dropped from 96.8M to 40.4M, but the
    kernel became memory/LG-throttle limited, with useful global store bytes per
    32-byte sector falling from 16.0 to 3.7.
- Reason: producer kernels are not simple output-store problems. The
  constant/vector warp mapping improved the isolated producer but worsened the
  following vector/vector stages, and the flat-depth chunking under-parallelized
  stores badly enough to erase the bit-unpack instruction savings.

### Root three-level top-subtree fusion

- Status: failed, rolled back
- Profiling setup: Nsight Systems on `json_api.json`, 256 MiB, 32 KiB blocks.
  The committed root-two fusion leaves one final level-2 wave before the fused
  root kernel. In the pre-change trace, that wave cost about 1.676 ms per decode
  (`scheduledFlatKernel<3>` about 0.331 ms, `scheduledMergeVectorVectorKernel`
  about 0.843 ms, and `scheduledMergeConstantVectorKernel` about 0.500 ms),
  followed by the packed root-two kernel at about 1.913 ms.
- Idea: add a shape-driven root-three fusion for the existing root plus two
  level-1 vector/vector shape. The new kernel skipped level 2 and resolved the
  four level-2 grandchildren directly inside the packed root loop, reading
  level-3 streams where needed.
- Result:
  - `json_api.json`, 256 MiB, 20 iterations regressed to 25.2316 GiB/s.
  - Nsight Systems showed `scheduledMergeRoot3VectorVectorKernel` taking
    32.853 ms over four decodes, about 8.213 ms per decode.
- Reason: inlining the grandchildren moved too much rank/select and child
  dispatch work into the byte-serial packed root loop. The removed level-2
  kernels plus the old root-two kernel cost about 3.6 ms per decode, while the
  root-three kernel alone cost more than 8 ms. A viable deeper fusion would need
  to preserve per-node/warp parallelism or precompute child positions, not just
  inline another tree level into the root byte loop.

### Directory and stage pruning audit

- Status: no code change
- Idea: after the producer and top-subtree experiments, look for directory or
  stage work made dead by the selected fused decode path.
- Result: with the failed root-three fusion rolled back, the selected generic
  path is still the packed root-two fusion. That path already skips GPU stage
  launches for levels 0 and 1. The remaining directory work is still live:
  the fused root-two kernel needs the root and both level-1 rank directories,
  and every lower internal node directory feeds a surviving merge kernel.
- Profiling context: the current `json_api.json` trace shows
  `scheduledDirectoryKernel` at about 1.06 ms per decode with `gridY = 22`.
  Removing root and level-1 directories would break the fused root kernel, and
  no level-2 stages can be skipped without the root-three fusion that regressed.
- Conclusion: there is no deterministic pruning opportunity in the current
  selected design. Directory/stage pruning should be revisited only after a
  deeper fusion strategy preserves per-node parallelism and actually removes
  downstream consumers.
- Baseline commit: `71a3d5ab9031` (`Add bottom-up GPU decode path`).
- Representative real-file gate, 256 MiB, 5 iterations, compared with the
  bottom-up chunked bitmap-byte snapshot above:
  - `cat-image.jpg`: 12.7302 to 22.5084 GiB/s
  - `dna_fasta.fa`: 19.3354 to 31.6990 GiB/s
  - `json_api.json`: 10.2090 to 19.2664 GiB/s
  - `cat-wiki.html`: 10.8595 to 16.4623 GiB/s
  - `calgary_pic`: 17.8374 to 37.6325 GiB/s
  - `log_apache.log`: 8.9345 to 18.7960 GiB/s
  - Geometric mean: 12.7761 to 23.3016 GiB/s (+82.4%)
- All real files, 256 MiB, 5 iterations:
  - `cat-image.jpg`: 12.7302 to 22.5084 GiB/s
  - `calgary_pic`: 17.8374 to 37.6325 GiB/s
  - `cat-wiki.html`: 10.8595 to 16.4623 GiB/s
  - `chinese_text.txt`: 9.1616 to 16.9250 GiB/s
  - `csv_numeric.csv`: 13.4258 to 23.5501 GiB/s
  - `dna_fasta.fa`: 19.3354 to 31.6990 GiB/s
  - `gzip_random.gz`: 265.1901 to 286.7709 GiB/s
  - `json_api.json`: 10.2090 to 19.2664 GiB/s
  - `log_apache.log`: 8.9345 to 18.7960 GiB/s
  - `pride.txt`: 8.5871 to 16.2005 GiB/s
  - `source_c.c`: 9.2200 to 17.6455 GiB/s
  - Geometric mean: 15.3604 to 26.8508 GiB/s (+74.8%)
- Notes: no real-file decode regressions were observed in this 5-iteration
  run. The flat-root fast path remains selected for `gzip_random.gz`.

### Parallel scheduled directory build

- Status: worked, pending commit
- Profiling setup: Nsight Systems CUDA trace on `json_api.json`, 256 MiB,
  32 KiB blocks, 1 measured iteration. The benchmark's three warmup decodes
  make decode kernels appear four times in the kernel summary.
- Bottleneck before change:
  - `scheduledMergeVectorVectorKernel`: 33.699 ms total over four decodes,
    about 8.425 ms per decode.
  - `scheduledParseKernel`: 21.690 ms total over four decodes, about
    5.423 ms per decode.
  - Parse was one thread per block and serially built every non-constant
    internal node's rank/select directory, which alone capped
    `json_api.json` below 50 GiB/s.
- Idea: split parsing from directory construction. The parse kernel now reads
  stored `numOnes`, assigns child counts and directory ranges, and only serially
  popcounts both-constant nodes where the bitstream does not store a count. A
  new node-scheduled directory kernel builds and validates popcount directories
  in parallel across `(internal node, block)` CTAs before bottom-up execution.
- Profiling after change on the same setup:
  - `scheduledParseKernel`: 1.687 ms total over four decodes, about
    0.422 ms per decode.
  - `scheduledDirectoryKernel`: 4.247 ms total over four decodes, about
    1.062 ms per decode.
  - `scheduledMergeVectorVectorKernel`: still about 8.425 ms per decode and is
    now the clear next bottleneck.
- Correctness:
  - `buck test --local-only @fbcode//mode/dev-nosan fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_test`
- Representative real files, 256 MiB, 5 iterations, compared with the
  node-scheduled baseline above:
  - `cat-image.jpg`: 22.5084 to 29.5978 GiB/s
  - `dna_fasta.fa`: 31.6990 to 52.8783 GiB/s
  - `json_api.json`: 19.2664 to 20.9403 GiB/s
  - `cat-wiki.html`: 16.4623 to 23.3491 GiB/s
  - `calgary_pic`: 37.6325 to 50.8752 GiB/s
  - `log_apache.log`: 18.7960 to 21.1174 GiB/s
  - Geometric mean: 23.3016 to 30.6071 GiB/s (+31.4%)
- Notes: an accidental pair of concurrent benchmark runs was discarded and the
  table above uses sequential reruns only. This is still far from 100 GiB/s;
  the next large target is vector/vector merge throughput.

### Skip both-constant scheduled popcount

- Status: kept, no measurable benchmark win
- Idea: in the scheduled parser, do not popcount both-constant merge bitmaps.
  The wire format stores no `numOnes` for those nodes, and the scheduled
  decoder's both-constant kernel emits directly from the bitmap plus the two
  constant symbols. The child counts are therefore unused because constant
  children are never materialized.
- Correctness:
  - `buck test --local-only @fbcode//mode/dev-nosan fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_test`
- Representative real files, 256 MiB, 20 iterations, compared with the
  parallel scheduled directory build run above:
  - `cat-image.jpg`: 29.5978 to 36.2814 GiB/s
  - `dna_fasta.fa`: 52.8783 to 43.1172 GiB/s
  - `json_api.json`: 20.9403 to 21.0114 GiB/s
  - `cat-wiki.html`: 23.3491 to 21.8185 GiB/s
  - `calgary_pic`: 50.8752 to 51.2406 GiB/s
  - `log_apache.log`: 21.1174 to 21.1502 GiB/s
  - Geometric mean: 30.6071 to 30.3220 GiB/s (-0.9%)
- Notes: this removes provably unused parse work, but the benchmark movement is
  within normal run noise and is not a meaningful throughput improvement. The
  vector/vector merge remains the dominant measured bottleneck.

### 16-symbol vector/vector merge chunks

- Status: failed, reverted
- Idea: for large scheduled vector/vector merge streams, have each thread
  process 16 output symbols from two bitmap bytes instead of 8 output symbols
  from one bitmap byte. This halves rank-directory lookups on root and
  near-root nodes, which dominated the `json_api.json` profile.
- Result:
  - `json_api.json`, 256 MiB, 20 iterations: 21.0114 to 15.9736 GiB/s.
  - Nsight Systems CUDA trace, 256 MiB, 1 measured iteration:
    `scheduledMergeVectorVectorKernel` rose from 33.699 ms to 49.799 ms total
    over the four decode passes.
- Reason: the saved rank-directory lookups did not pay for the extra serial
  work per thread and reduced effective parallelism in the large merge levels.
  The current 8-symbol bitmap-byte chunk is a better point on this GPU.

### Constant/vector bitmap-byte chunks

- Status: failed, reverted
- Idea: apply the 8-symbol bitmap-byte merge used by large vector/vector nodes
  to constant/vector and vector/constant merge nodes.
- Result: the measurements were inconsistent. A full representative run with
  the simple 1024-symbol threshold had a better geometric mean, but reruns and
  an isolation pass showed dataset-dependent regressions, especially on
  `dna_fasta.fa`. A tableLog-based selector avoided some regressions but did
  not preserve the apparent `json_api.json` and `cat-wiki.html` wins.
- Profiling note: Nsight Systems did not show the intended
  `scheduledMergeConstantVectorKernel` improvement on `json_api.json`; it was
  still about 3.8 ms total over the four decode passes. Since this did not move
  the profiled bottleneck and required a heuristic selector, the code was
  reverted.

### Root two-level vector/vector fusion

- Status: worked, pending commit
- Profiling setup: Nsight Systems CUDA trace on `json_api.json`, 256 MiB,
  32 KiB blocks, 1 measured iteration. The benchmark's three warmup decodes
  make decode kernels appear four times in the kernel summary.
- Bottleneck before change: after the parallel directory build,
  `scheduledMergeVectorVectorKernel` was the dominant cost at 33.699 ms total
  over four decodes. The last two vector/vector launches in each decode were
  about 2.97 ms and 2.67 ms, more than half of decode kernel time.
- Idea: when the root and both level-1 children are vector/vector merge nodes,
  skip the level-1 and root materialization stages and replace them with one
  fused root kernel. The fused kernel reads the root and level-1 bitmaps plus
  level-2 streams and writes final output directly, avoiding a full-block
  intermediate stream.
- Profiling after change:
  - `scheduledMergeVectorVectorKernel`: 11.084 ms total over 28 launches.
  - `scheduledMergeRoot2VectorVectorKernel`: 16.794 ms total over four
    launches, about 4.199 ms per decode.
  - Combined vector/vector merge time fell from 33.699 ms to 27.878 ms over the
    four profiled decodes, saving about 1.46 ms per decode on `json_api.json`.
- Correctness:
  - `buck test --local-only @fbcode//mode/dev-nosan fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_test`
- Representative real files, 256 MiB, 20 iterations, compared with the
  preceding committed run:
  - `cat-image.jpg`: 36.2814 to 36.1988 GiB/s
  - `dna_fasta.fa`: 43.1172 to 42.9693 GiB/s
  - `json_api.json`: 21.0114 to 23.9586 GiB/s
  - `cat-wiki.html`: 21.8185 to 25.7336 GiB/s
  - `calgary_pic`: 51.2406 to 51.2503 GiB/s
  - `log_apache.log`: 21.1502 to 29.8154 GiB/s
  - Geometric mean: 30.3220 to 33.7022 GiB/s (+11.1%)
- All real files, 256 MiB, 20 iterations:
  - `cat-image.jpg`: 29.4268 GiB/s
  - `calgary_pic`: 51.3926 GiB/s
  - `cat-wiki.html`: 24.9855 GiB/s
  - `chinese_text.txt`: 25.5399 GiB/s
  - `csv_numeric.csv`: 41.5466 GiB/s
  - `dna_fasta.fa`: 43.1272 GiB/s
  - `gzip_random.gz`: 246.9965 GiB/s
  - `json_api.json`: 23.9453 GiB/s
  - `log_apache.log`: 24.1756 GiB/s
  - `pride.txt`: 24.9968 GiB/s
  - `source_c.c`: 23.7771 GiB/s
  - Geometric mean versus the last all-real table in this log: 26.8508 to
    36.3623 GiB/s (+35.4%).
- Notes: `gzip_random.gz` uses the separate flat-root fast path and is not
  affected by the fusion selector; its lower number versus the older all-real
  table appears to be run-to-run variance in that unchanged path. The fusion is
  deliberately selected only for the exact root plus two level-1 vector/vector
  shape.

### Fused root child-bit preloading

- Status: failed, reverted
- Idea: inside the fused root two-level vector/vector kernel, load the next
  level-1 child bitmap bits into registers once per root bitmap byte instead of
  calling `dGetBit` for each emitted symbol.
- Result: `json_api.json`, 256 MiB, 20 iterations regressed from the root
  fusion run to 23.5752 GiB/s. Nsight Systems showed
  `scheduledMergeRoot2VectorVectorKernel` rising from about 4.199 ms to
  4.353 ms per decode.
- Reason: the extra unaligned child-bit load and register pressure outweighed
  the removed per-symbol byte/bit extraction in the fused kernel.

### Nsight Compute `mergeVectorVector` bottleneck profile

- Status: profiling evidence, no code change
- Setup: muted DCGM profiling with
  `sudo dyno dcgm_profiling --mute=true --duration=1200_s`, then profiled the
  direct Buck-built `pivco_gpu_bench` binary with `/usr/local/cuda/bin/ncu`
  because profiling through `buck run` did not attach to the benchmark child
  process reliably.
- Large remaining `scheduledMergeVectorVectorKernel` launch on `json_api.json`,
  256 MiB, 32 KiB blocks:
  - Duration: 1.47 ms for grid `(8192, 2, 1)`.
  - DRAM throughput: 10.5% of peak, 203.5 GB/s.
  - L2 throughput: 69.1% of peak.
  - Compute throughput: 19.9% of peak.
  - Achieved occupancy: 92.0%, but only 0.36 eligible warps per scheduler and
    82.7% cycles with no eligible warp.
  - Dominant stalls: long scoreboard 50.75 cycles per issued instruction and
    LG throttle 27.60.
  - Memory transaction shape: global loads average 2.17 useful bytes per
    32-byte sector, global stores average 4.01 bytes per sector. L2 load hit
    rate is only 10.8%, while store hit rate is 99.3%.
  - Device memory moved by the kernel: 169.9 MB read and 128.2 MB written.
- Fused `scheduledMergeRoot2VectorVectorKernel` launch on the same data:
  - Duration: 4.20 ms for grid `(8192, 1, 1)`.
  - DRAM throughput: 8.15% of peak, 157.8 GB/s.
  - L2 throughput: 68.7% of peak.
  - Compute throughput: 38.8% of peak.
  - Achieved occupancy: 96.1%, but only 0.76 eligible warps per scheduler and
    67.0% cycles with no eligible warp.
  - Dominant stalls: long scoreboard 30.55 cycles per issued instruction and
    LG throttle 9.10.
  - Memory transaction shape: global loads average 2.42 useful bytes per
    32-byte sector, global stores average 2.83 bytes per sector.
- Conclusion: current `mergeVectorVector` is not limited by raw HBM bandwidth.
  The limiting behavior is byte-granular, poorly coalesced load/store traffic
  combined with dependent rank/select addressing. Large next steps should focus
  on changing the merge layout or work mapping to produce coalesced vectorized
  writes and reduce rank-dependent stream gathers, not on small arithmetic or
  popcount tweaks.

### Warp-mapped vector/vector merge

- Status: kept
- Idea: remap large `scheduledMergeVectorVectorKernel` nodes from one thread
  per bitmap byte with an eight-symbol serial loop to one warp per 32-bit
  bitmap word, with one lane producing one output symbol. This keeps a single
  generic vector/vector implementation for large nodes while improving output
  coalescing and removing the per-thread serial byte loop.
- Nsight Compute on the same large `scheduledMergeVectorVectorKernel` launch as
  the prior profile, `json_api.json`, 256 MiB, 32 KiB blocks:
  - Duration: 1.47 ms to 1.13 ms.
  - DRAM throughput: 10.5% to 13.0% of peak.
  - L2 throughput: 69.1% to 30.0% of peak.
  - Compute throughput: 19.9% to 63.2% of peak.
  - Achieved occupancy: 92.0% to 96.9%.
  - Eligible warps per scheduler: 0.36 to 1.74; cycles with no eligible warp:
    82.7% to 40.4%.
  - Dominant stalls improved from long scoreboard 50.75 and LG throttle 27.60
    to long scoreboard 18.41 and LG throttle 0.04 cycles per issued
    instruction.
  - Average active threads per warp: 28.37 to 31.99; average predicated-on
    threads per warp: 21.38 to 28.24.
  - Global load bytes per sector: 2.17 to 4.64. Global store bytes per sector:
    4.01 to 16.56.
  - Instruction count increased from 123.7M to 328.1M for this launch, but the
    lower memory dependency stalls and much better store coalescing made the
    kernel faster.
- Representative real files, 256 MiB, 20 iterations, compared with the prior
  representative table in this log:
  - `cat-image.jpg`: 36.1988 to 33.4852 GiB/s
  - `dna_fasta.fa`: 42.9693 to 46.6897 GiB/s
  - `json_api.json`: 23.9586 to 24.9174 GiB/s
  - `cat-wiki.html`: 25.7336 to 26.0806 GiB/s
  - `calgary_pic`: 51.2503 to 51.9559 GiB/s
  - `log_apache.log`: 29.8154 to 25.7125 GiB/s
  - Geometric mean: 33.7022 to 33.2746 GiB/s (-1.3%). This run showed large
    run-to-run variance on `cat-image.jpg` and `log_apache.log`; the full-real
    run below was used with the profiler evidence to decide whether to keep the
    direction.
- All real files, 256 MiB, 20 iterations, compared with the prior all-real
  table in this log:
  - `cat-image.jpg`: 29.4268 to 35.2753 GiB/s
  - `calgary_pic`: 51.3926 to 51.8565 GiB/s
  - `cat-wiki.html`: 24.9855 to 31.6901 GiB/s
  - `chinese_text.txt`: 25.5399 to 26.7551 GiB/s
  - `csv_numeric.csv`: 41.5466 to 36.8960 GiB/s
  - `dna_fasta.fa`: 43.1272 to 46.5917 GiB/s
  - `gzip_random.gz`: 246.9965 to 246.1095 GiB/s
  - `json_api.json`: 23.9453 to 24.9072 GiB/s
  - `log_apache.log`: 24.1756 to 25.8596 GiB/s
  - `pride.txt`: 24.9968 to 26.2573 GiB/s
  - `source_c.c`: 23.7771 to 27.5026 GiB/s
  - Geometric mean: 36.3623 to 38.8605 GiB/s (+6.9%).
- Notes: `csv_numeric.csv` regressed by 11.2% in this run. Per user direction,
  this change intentionally does not add a selector between the byte-loop and
  warp-mapped implementations; keep one implementation and continue improving
  the vector/vector path.
- Correctness:
  - `buck test --local-only @fbcode//mode/dev-nosan fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_test`

### Warp load broadcast inside vector/vector merge

- Status: failed, reverted
- Idea: after the warp-mapped vector/vector merge, have lane 0 load the
  directory entry and bitmap word once and broadcast them with `__shfl_sync`,
  instead of letting every lane load the same cached values.
- Result: representative real-file timing regressed versus the plain warp
  mapping:
  - `cat-image.jpg`: 32.6749 GiB/s
  - `dna_fasta.fa`: 45.8768 GiB/s
  - `json_api.json`: 24.7287 GiB/s
  - `cat-wiki.html`: 31.3384 GiB/s
  - `calgary_pic`: 51.6505 GiB/s
  - `log_apache.log`: 25.5240 GiB/s
- Reason: the extra shuffle and lane-0 dependency cost more than the redundant
  cached directory/bitmap loads in this kernel.

### Warp-mapped fused root vector/vector merge

- Status: failed, reverted
- Idea: apply the same one-warp-per-32-root-bits mapping to
  `scheduledMergeRoot2VectorVectorKernel`. Each warp would load a 32-bit root
  word, derive contiguous left/right child windows, and use child rank bases
  plus per-lane popcounts to write final output positions directly.
- Result: Nsight Compute on `json_api.json`, 256 MiB, 32 KiB blocks showed a
  clear regression:
  - `scheduledMergeRoot2VectorVectorKernel` duration rose from 4.20 ms to
    8.64 ms.
  - DRAM throughput fell from 8.15% to 3.79% of peak.
  - L2 throughput fell from 68.7% to 11.1% of peak.
  - Compute throughput rose from 38.8% to 80.8% of peak.
  - Eligible warps per scheduler improved from 0.76 to 6.04, but average
    active threads per warp collapsed from 21.49 to 7.64.
- Reason: the rewrite traded the original memory-pressure problem for a much
  worse compute/control problem. The child-window rank setup and warp shuffles
  left most lanes inactive for large parts of the instruction stream, so the
  kernel became compute dominated and about 2x slower.

### Current decode timeline breakdown after warp-mapped vector/vector merge

- Status: profiling evidence, no code change
- Setup: Nsight Systems CUDA trace on `json_api.json`, 256 MiB, 32 KiB blocks,
  1 measured iteration. The benchmark emits three warmup decodes plus the
  measured decode; steady-state numbers below exclude the first decode's
  inflated CPU launch API time.
- Steady-state decode timeline:
  - GPU decode span: 10.098 ms.
  - Decode kernel sum: 10.059 ms.
  - GPU gaps between decode kernels: 0.038 ms.
  - Decode kernel launches: 30 per decode.
  - CPU-side `cudaLaunchKernel` API time for those launches: 0.257 ms per
    decode. This is not the dominant device-timeline cost today; most launch
    API time is hidden because GPU gaps are only about 0.04 ms.
- Kernel breakdown per decode:
  - `scheduledMergeRoot2VectorVectorKernel`: 4.202 ms, 41.8%.
  - `scheduledMergeVectorVectorKernel`: 2.344 ms, 23.3%.
  - `scheduledDirectoryKernel`: 1.062 ms, 10.6%.
  - `scheduledFlatKernel<*>`: 1.051 ms, 10.4%.
  - `scheduledMergeConstantVectorKernel`: 0.929 ms, 9.2%.
  - `scheduledParseKernel`: 0.410 ms, 4.1%.
  - `scheduledMergeConstantConstantKernel`: 0.062 ms, 0.6%.
- One steady-state decode launch sequence ends with the largest kernels:
  - `scheduledMergeVectorVectorKernel`, grid `(8192, 2, 1)`: 0.593 ms.
  - `scheduledMergeVectorVectorKernel`, grid `(8192, 2, 1)`: 1.146 ms.
  - `scheduledMergeConstantVectorKernel`: 0.498 ms.
  - `scheduledMergeRoot2VectorVectorKernel`: 4.192 ms.
- Host/device transfer components in the profiled benchmark are outside the
  decode kernel span:
  - Host-to-device copies: 83.782 ms total over 12 copies for the full
    encode/decode benchmark process.
  - Device-to-host copies: 29.090 ms total over four copies.
  - CUDA memsets: 0.009 ms total.
- Conclusion: the next large optimization target is still the top of the tree,
  especially `scheduledMergeRoot2VectorVectorKernel`. However, the failed
  warp-mapped root rewrite shows that simply making root output stores
  coalesced is not enough if it adds child-window rank setup and lane
  under-utilization. A better next root strategy needs to keep high lane
  utilization while reducing the root kernel's byte-granular stream gathers.

### Disable root vector/vector fusion after warp-mapped regular merges

- Status: failed, reverted
- Idea: after the regular vector/vector merge became faster, disable
  `fuseRootVectorVector` entirely and let the same warp-mapped
  `scheduledMergeVectorVectorKernel` handle the two level-1 nodes and root as
  ordinary stages.
- Result: `json_api.json`, 256 MiB, 20 iterations regressed to 24.1723 GiB/s
  versus the current root-fused path's recent 24.5-24.9 GiB/s range.
- Reason: despite being the biggest remaining kernel, the fused root path still
  beats re-materializing the level-1 streams and root as separate regular
  vector/vector stages.

### Packed final stores in fused root vector/vector merge

- Status: kept
- Idea: keep the existing `scheduledMergeRoot2VectorVectorKernel` byte-loop and
  child rank logic, but pack each full eight-symbol output group into one
  64-bit final-output store. This targets the same store-sector inefficiency
  fixed in the regular vector/vector merge without remapping the root kernel to
  one lane per output. The implementation falls back to byte stores for the
  tail and for unaligned destination addresses.
- Nsight Compute on `scheduledMergeRoot2VectorVectorKernel`, `json_api.json`,
  256 MiB, 32 KiB blocks:
  - Duration: 4.20 ms to 1.91 ms.
  - DRAM throughput: 8.15% to 17.2% of peak.
  - L2 throughput: 68.7% to 19.0% of peak.
  - Compute throughput: 38.8% to 86.9% of peak.
  - Eligible warps per scheduler: 0.76 to 5.46.
  - Cycles with no eligible warp: 67.0% to 27.5%.
  - Average active threads per warp: 21.49 to 23.31.
  - Global store bytes per sector: 2.83 to 32.00.
- Representative real files, 256 MiB, 20 iterations, compared with the prior
  representative table in this log:
  - `cat-image.jpg`: 33.4852 to 33.4990 GiB/s
  - `dna_fasta.fa`: 46.6897 to 46.3969 GiB/s
  - `json_api.json`: 24.9174 to 38.9503 GiB/s
  - `cat-wiki.html`: 26.0806 to 38.6788 GiB/s
  - `calgary_pic`: 51.9559 to 51.6919 GiB/s
  - `log_apache.log`: 25.7125 to 33.7211 GiB/s
  - Geometric mean: 33.2746 to 39.9765 GiB/s (+20.1%).
- All real files, 256 MiB, 20 iterations, compared with the prior all-real
  table in this log:
  - `cat-image.jpg`: 35.2753 to 33.4898 GiB/s
  - `calgary_pic`: 51.8565 to 51.8455 GiB/s
  - `cat-wiki.html`: 31.6901 to 34.3183 GiB/s
  - `chinese_text.txt`: 26.7551 to 36.3683 GiB/s
  - `csv_numeric.csv`: 36.8960 to 36.8681 GiB/s
  - `dna_fasta.fa`: 46.5917 to 46.6184 GiB/s
  - `gzip_random.gz`: 246.1095 to 285.8790 GiB/s
  - `json_api.json`: 24.9072 to 37.6122 GiB/s
  - `log_apache.log`: 25.8596 to 36.4172 GiB/s
  - `pride.txt`: 26.2573 to 40.0954 GiB/s
  - `source_c.c`: 27.5026 to 31.9263 GiB/s
  - Geometric mean: 38.8605 to 45.8155 GiB/s (+17.9%).
- Post-change Nsight Systems steady-state breakdown on `json_api.json`:
  - GPU decode span: 7.804 ms.
  - Decode kernel sum: 7.764 ms.
  - GPU gaps between decode kernels: 0.040 ms.
  - `scheduledMergeVectorVectorKernel`: 2.343 ms, 30.2%.
  - `scheduledMergeRoot2VectorVectorKernel`: 1.907 ms, 24.6%.
  - `scheduledDirectoryKernel`: 1.062 ms, 13.7%.
  - `scheduledFlatKernel<*>`: 1.050 ms, 13.5%.
  - `scheduledMergeConstantVectorKernel`: 0.929 ms, 12.0%.
  - `scheduledParseKernel`: 0.411 ms, 5.3%.
  - `scheduledMergeConstantConstantKernel`: 0.062 ms, 0.8%.
- Follow-up check: after adding the unaligned-destination byte-store fallback,
  `json_api.json`, 256 MiB, 20 iterations measured 37.3761 GiB/s, consistent
  with the packed-store improvement.
- Next target: regular vector/vector is now the largest decode bucket again,
  followed by the packed root kernel and then directory/flat/constant-vector
  work. Further large gains likely need another reduction in regular
  vector/vector gather/rank work or a broader top-of-tree fusion that preserves
  the packed root store behavior.
- Correctness:
  - `buck test --local-only @fbcode//mode/dev-nosan fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_test`

### Block-resident single-CTA decode (shared-memory pivot)

- Status: failed, reverted (kept as a documented negative result)
- Hypothesis: the scheduled path is not HBM-bound (DRAM 10-17% of peak) but
  pays ~2x-tree-depth global round-trips of the block through `bufferA/bufferB`
  plus byte-granular, rank-dependent child gathers. A 32 KiB block's working set
  fits in A100 shared memory, so decoding each block in ONE CTA with all
  bitmaps/rank-directories/intermediate streams on-chip should cut global
  traffic to the theoretical minimum (read slice once, write output once) and
  turn the scattered global gathers into cheap shared accesses.
- Baselines re-measured on this box (median GiB/s, 256 MiB, 20 iters):
  `json_api.json` 33.1, `cat-image.jpg` 33.5, `cat-wiki.html` 34.2,
  `calgary_pic` 52.0, `dna_fasta.fa` 46.6, `log_apache.log` 33.7.
- Variant A -- top-down per-symbol rank/select walk, everything in shared. Each
  thread walks root->leaf for its own output positions (coalesced stores, no
  barriers). Result on `json_api.json`, 256 MiB: 12.0 GiB/s.
  - Nsight Compute: the hypothesis held on memory -- DRAM 0.87% of peak, memory
    throughput 29% -- but the kernel became compute/issue bound: Compute (SM)
    74.6%, IPC 2.71, 8.5 G issued instructions for the launch (~33 instr per
    output byte) from the deep per-symbol dependent rank chain
    (`dLoadLe32Masked` byte loop + `dGetBits` bit loop x tableLog levels). At
    ~67% of peak issue rate, this is fundamentally issue-limited; a tree walk
    cannot reach ~4 instr/byte.
- Variant B -- bottom-up warp-cooperative merge in shared ping-pong buffers (the
  GPU analog of the CPU AVX512 `vpexpandb` path). Single-pass cooperative merge
  per node. Result on `json_api.json`, 256 MiB: 18.5 GiB/s.
  - Nsight Compute: DRAM 1.34% (memory again free), Compute 44%, but achieved
    occupancy only 24.8% (Block Limit Shared Mem = 2 CTAs/SM from the 2xblockSize
    = 64 KiB buffers) and 56.8% of cycles had no eligible warp. Bound by the
    per-node cooperative rank-scan barrier storm (log-depth `__syncthreads` x
    ~190 internal nodes/block) starving the few resident warps.
  - Smaller blocks made it worse (16 KiB 16.8, 8 KiB 14.8 GiB/s): the tree node
    count is fixed by the weights, so smaller blocks give the same ~190 nodes
    fewer symbols each, so the per-node barrier overhead dominates even more.
- Variant B2 -- split each level into a cooperative pass for the few large
  near-root nodes and a warp-parallel pass (warp-scan rank, no block barriers)
  for the many small deep-tree nodes. Result on 256 MiB: `json_api.json` 15.9,
  `calgary_pic` 21.2, `dna_fasta.fa` 23.7 GiB/s -- warp-serial small-node
  processing regressed json vs the single-pass merge and stayed far below
  baseline. (A full-mask `__shfl_sync` executed under `if (lane == 0)`
  dead-locked the first build; noted as a gotcha.)
- Conclusion: keeping a block resident in shared memory does remove the memory
  bottleneck (verified: DRAM < 2% of peak in every variant), but a single CTA
  per 32 KiB deep-tree block cannot match the scheduled path's throughput. The
  scheduled path's strength is massive `(block x node)` grid parallelism that
  hides latency at 92-96% occupancy; folding a block into one CTA trades that
  away for an intra-block serial/barrier/occupancy bottleneck. The decode is
  latency/issue bound per symbol, not bandwidth bound, so the well-tuned
  scheduled path is at or near the practical ceiling for mixed deep trees on this
  GPU. Reverted to the scheduled decoder.

### Kernel-by-kernel latency/instruction optimization pass

Note on measurement: this box shows large run-to-run variance (GPU clock;
`log_apache.log` was seen bouncing 37-45 GiB/s across identical runs, `calgary_pic`
57-70). Each result below is a back-to-back A/B against the immediately prior
build; absolute cross-session numbers are not comparable.

- Vector/vector merge, 2-way MLP -- kept. `ncu` on the large
  `scheduledMergeVectorVectorKernel` launches: DRAM 13% of peak, L1 hit 88%,
  L2 hit 82%, long-scoreboard 71% of stall cycles at ~97% occupancy -- i.e. not
  bandwidth bound but latency bound on the `directory` load -> data-dependent
  child gather. Processing two independent bitmap words per thread (both gathers
  issued before either store) doubles memory-level parallelism. Back-to-back
  (256 MiB, 10 iters): `cat-image.jpg` 33.5->38.2, `dna_fasta.fa` 46.6->50.8,
  `log_apache.log` 33.7->36.0, `cat-wiki.html` 34.2->36.2, ~+7% geomean, no
  regressions.
  - A prior attempt to coalesce the same gather by staging child streams into
    shared memory REGRESSED ~15%: the child streams are L1/L2-resident, so the
    "uncoalesced" 4.6 bytes/sector load is not the cost -- the staging copy plus
    the 32 KiB shared occupancy hit is. Latency, not bandwidth, is the limiter.
- Flat-leaf windowed bit extraction -- kept. `scheduledFlatKernel<Depth>`
  replaced `dGetBits`' bit-by-bit loop (which re-reads the same 1-2 bytes Depth
  times) with a single windowed 1-2 byte read + shift + mask. Back-to-back:
  `cat-image.jpg` 38.2->43.8, `chinese_text.txt` 38.4->39.8, others +1-3%, no
  regressions. Unlike the earlier reverted windowed attempt this changes only the
  read path (not the store mapping), so `pride.txt`/`source_c.c` do not regress.
- Constant/vector and vector/constant merges, warp-mapped -- kept. These used a
  scalar thread-per-output loop with a `directory` load per output; converted the
  large-node path to the warp-per-32-bit-word mapping (one dir load per 32
  outputs) plus 2-way MLP. Back-to-back: `calgary_pic` 53.8->69.6 (+29%),
  `log_apache.log` 36.6->44.7 (+22%), `dna_fasta.fa` 50.8->52.9, no regressions.
  The earlier-logged constant/vector warp-map regression of downstream
  vector/vector did NOT recur (the vector/vector merge is now itself warp-mapped).
- Fused root-two, 2-way MLP -- reverted. `ncu` shows
  `scheduledMergeRoot2VectorVectorKernel` is COMPUTE bound (SM 87%, DRAM 17%),
  not latency bound, so unrolling by two bytes gave only ~+1%; not worth the added
  register pressure. Root2's cost is the serial 8-bit routing loop; reducing its
  instruction count (not hiding latency) is the only lever, and it is already
  packed-store optimized.
- Disable root-two fusion (let the now-faster warp-mapped vector/vector handle
  root + level 1) -- reverted. Regressed the fusion-eligible datasets ~21%
  (`json_api.json` 35->28, `cat-wiki.html` 37->29, `chinese_text.txt`,
  `pride.txt`), because it re-materializes two full-block level-1 streams. Even
  compute-bound, the fused kernel beats three separate merge passes. Only
  `source_c.c` improved (+9%).
- Net: the three kept changes give roughly +10-20% geomean over the prior
  scheduled baseline (dataset-dependent; skewed/flat-heavy files gain most). The
  merges are latency- or compute-bound on cache-resident data at high occupancy,
  so the effective levers are memory-level parallelism (VV, CV/VC) and
  instruction reduction (flat), not coalescing.

### Thread-per-byte contiguous-stream merge (breakthrough)

- Status: kept -- largest single win.
- Idea (contiguous stream reads + register-shift selection, the GPU analog of
  the CPU merge): give each thread one bitmap byte (8 outputs) instead of the
  warp-per-word mapping. Read the rank once, then pull up to 8 CONTIGUOUS bytes
  from each child stream into a u64 register (the most either child can supply
  for 8 outputs) and select per bit by shifting the bottom byte out of the chosen
  child. Result per 8 outputs: two contiguous child loads + register shifts + one
  coalesced 8-byte store, versus eight scattered, dependent child gathers. All
  routing stays in registers. `dLoad8Bounded` keeps the reads inside the child
  stream. Applied to vector/vector, constant/vector, vector/constant.
- This is why it beats the warp-per-word gather: the scattered gather's cost was
  the per-output dependent load latency (long-scoreboard), not bandwidth;
  replacing 8 dependent scattered loads with 2 independent contiguous loads
  collapses the stall.
- Decode throughput, 256 MiB, 10 iters, vs the warp-mapped merges:
  `dna_fasta.fa` 52.9->77.9 (+47%), `cat-image.jpg` 44.0->60.9 (+38%),
  `calgary_pic` 57->76.3 (+34%), `csv_numeric.csv` 41.9->56.6 (+35%),
  `source_c.c` 31.4->42.5 (+35%), `json_api.json` 35.1->39.3,
  `cat-wiki.html` 37.1->40.6 GiB/s.

### Fused root-two: window child partition bits

- Status: kept. Root2 is compute bound (SM 87%); its inner loop called `dGetBit`
  8x per root byte. Load each child's <=8 partition bits into a register once
  (LSB-first, second byte read only when the bits span it) and consume by
  shifting; grandchild streams still gathered per output. +3-4% on Root2-using
  datasets (`json_api.json`, `cat-wiki.html`, `pride.txt`, `chinese_text.txt`),
  `source_c.c` more.
- A full-window variant that also cached the four grandchild streams in u64
  registers REGRESSED ~5% -- the extra wide-register pressure dropped occupancy
  on this already register-heavy, compute-bound kernel. Grandchild gathers hit L1
  and are cheap to leave per-output.

### `__byte_perm` merge (four outputs per instruction)

- Status: kept. Replace the eight-iteration register selection loop in the byte
  merge with two `__byte_perm` (PRMT) instructions, each merging four outputs.
  A 16-entry selector table (`kMergeSel`, indexed by the 4-bit partition mask)
  permutes the four output bytes from {left bytes : right bytes} in one hardware
  op; a constant child is supplied as a replicated byte, and the child windows
  advance by the mask popcount between the two groups.
- Decode throughput, 256 MiB, 10 iters, vs the prior commit (no regressions):
  `calgary_pic` 76.4->83.0 (+8.6%), `cat-image.jpg` 60.9->64.8 (+6.4%),
  `dna_fasta.fa` 77.8->82.6 (+6.2%), `csv_numeric.csv` 56.5->59.2,
  `json_api.json` 40.4->41.5, `cat-wiki.html` 41.9->42.7 GiB/s.

### Cumulative result

- All real datasets, 256 MiB, 20 iters, versus the original scheduled baseline
  (`BENCHMARKS.md` / start-of-session reruns), noting ~+-15% run-to-run GPU-clock
  variance on this box:
  `cat-image.jpg` 33.5->64.8 (+93%), `dna_fasta.fa` 46.6->83.1 (+78%),
  `calgary_pic` 52.0->83.4 (+60%), `source_c.c` ->52.9, `csv_numeric.csv`
  ->59.8, `log_apache.log` 33.7->49.4 (+47%), `chinese_text.txt` ->45.8,
  `pride.txt` ->43.2, `cat-wiki.html` 34.2->42.9 (+25%), `json_api.json`
  33.1->41.4 (+25%); `gzip_random.gz` 287 (flat-root, unchanged). Roughly +50%
  geomean on the representative set, up to ~1.9x (`cat-image.jpg`,
  `dna_fasta.fa`).
- Takeaways: the merges were latency/compute bound on cache-resident data, not
  bandwidth bound. The winning levers were (1) turning the scattered per-output
  child gather into two contiguous per-byte loads + register-shift / `__byte_perm`
  selection, (2) amortizing the rank-directory load, (3) instruction reduction
  via windowed bit extraction. Coalescing mattered as *fewer independent
  contiguous loads* (killing the dependency chain), not as sector utilization.

### Kernel anti-pattern sweep

Systematic per-kernel audit (heaviest first), each fix profiled and gated.

- Rank-directory scan: replaced the O(entries log entries) Hillis-Steele block
  scan with a single-warp shuffle scan for small nodes (<=32 entries, no
  barriers) and a work-efficient two-level scan for large ones, plus 2-way MLP on
  the popcount loads. `scheduledDirectoryKernel` Compute (SM) 76%->54%; the
  kernel is now latency bound on its many small per-node CTAs.
- Scheduled parse: dropped the up-front zeroing of all node states (only `count`
  is read before write, and corrupt input is gated by `status`) and folded the
  per-level stream-offset assignment into the single pre-order pass, removing an
  O(levels*nodes) second pass. `scheduledParseKernel` 245->139 us; `calgary_pic`
  94->101, `dna_fasta.fa` 97->102 GiB/s.
- Flat merge (`scheduledFlatKernel`): cache the leaf's 2^Depth symbols in shared
  so the per-output table lookup hits shared. `csv_numeric.csv` 67->71,
  `chinese_text.txt` 53->56.
- Flat-root fast path (`fastDecodeFlatRootKernel`): the depth<8 path used
  per-symbol `dGetBits`; replaced with windowed extraction + shared symbols.
  `flat_M7` 61->227 (+3.7x), `flat_M6` 75->230, `sparse_16` 94->238, `flat_M3`
  120->240 GiB/s.
- Removed dead `rankSelectDecodeKernel` and `topDownDecodeKernel` (defined but
  never launched; superseded by the scheduled decoder).
- Audited but left as-is: the vector/vector merge (now bandwidth bound on the
  bottom-up child-read/output-write traffic, ~44% DRAM on large nodes -- at its
  floor) and the `decodeKernel`/`bottomUpDecodeKernel` correctness fallbacks
  (not reachable at the 32 KiB benchmark block size).

## Top-down scatter decoder (exploration, `PIVCO_TOPDOWN=1`)

An env-gated alternative decode path that inverts the dataflow: start every block
with the identity index list `0,1,2,...`; each internal node partitions its index
list into a left and a right child list by the node bitmap (rank/select), a
constant child scatters its symbol to `dst[idx]` instead of materializing a list,
a both-constant node scatters one of two symbols, and a flat leaf scatters one of
its `2^Depth` symbols. It reuses the scheduled parse + rank directory, then runs
one partition/flat kernel per (level, op).

Kept the bottom-up learnings and profiled each step:

- **Word reconstruction via `__ballot`.** The first version had all 32 lanes of a
  warp redundantly assemble the same masked 32-bit bitmap word
  (`dLoadLe32Masked` x32). A lane-0 load + `__shfl` broadcast was *slower*
  (serializes on lane 0's latency). The win was to have each lane read only its
  own bit and reconstruct the word with `__ballot_sync` -- less compute, no
  serialization, coalesced byte loads. `json_api.json` 19.7->23.1,
  `calgary_pic` 43.6->48.3, `dna_fasta.fa` 42.9->47.8 GiB/s.
- **2-way MLP in the partition.** The partition is latency bound (52%
  no-eligible-warp). Processing two words per warp iteration -- both ballots and
  both directory/index-list loads issued before the dependent stores -- overlaps
  the memory latency. `dna_fasta.fa` 47.8->54.7, `calgary_pic` 48.3->53.1,
  `cat-wiki.html` 22.0->26.0, `json_api.json` 23.1->24.4 GiB/s.

### Result: top-down is structurally ~2x slower for real Huffman data

After tuning, decode ratio (top-down / bottom-up), 128-256 MiB:

- **Flat / uniform distributions -- parity (~1.0x):** `flat_M3` 1.08, `flat_M5`
  0.98, `flat_M7` 1.00, `uniform` 1.00, `sparse_16` 1.00, `two_sym_eq` 0.82.
  These have no multi-level index-list traffic and their terminal writes are
  dense, so top-down matches (and `flat_M3` slightly beats) bottom-up.
- **Real Huffman trees -- ~0.5x:** `calgary_pic` 0.53, `dna_fasta.fa` 0.54,
  `cat-image.jpg` 0.51, `csv_numeric.csv` 0.51, `chinese_text.txt` 0.48,
  `json_api.json` 0.48, `log_apache.log` 0.48, `pride.txt` 0.47, `source_c.c`
  0.48, `cat-wiki.html` 0.54.
- **Skewed distributions -- 0.38-0.49x:** `zipfian` 0.45, `geometric` 0.46,
  `bell_s30` 0.49, `proba14` 0.46, `proba50` 0.41, `proba80` 0.38 (bottom-up is
  fastest exactly where top-down is relatively worst).

The ratio is remarkably consistent, which is the tell that it is structural, not a
tuning gap. Two fundamental costs, confirmed by ncu:

1. **~2x intermediate traffic.** Top-down moves `uint16` index lists across every
   tree level (2 bytes/position/level); bottom-up moves `uint8` symbol streams
   (1 byte/position/level).
2. **Scattered terminal writes.** Every constant/flat/both-constant leaf writes
   `dst[idx]` at rank-permuted addresses. ncu on the flat scatter: DRAM 62%
   (1.2 TB/s) but 42% excess sectors, stores 10.9/32 effective. Bottom-up writes
   output in position order (coalesced). This is irreducible in a scatter design
   -- the whole point is that leaf positions are spread across the block.

The one untried structural lever, partition-two-levels-per-kernel (level fusion),
only attacks cost (1) and is bounded (best case ~0.63x on the partition-heavy
`json`); it cannot touch the fundamental scatter of cost (2), and near-root
multi-child windowing already regressed from register pressure in the bottom-up
`Root2` work. Per the "no small refinements unless they unlock multiples"
guidance it was not pursued.

**Conclusion:** the bottom-up scheduled merge remains the production decode path.
Top-down reaches parity only for flat/uniform data; for actual entropy-coded data
it is ~2x slower for structural (not tunable) reasons. The path is retained behind
`PIVCO_TOPDOWN=1` for reference/experimentation.

## Bottom-up round 2 (profile-driven, AVX512-inspired)

Re-profiled the production path fresh. json decode breakdown (per decode):
vector/vector merge ~47%, rank directory ~20%, flat3 ~11%, constant/vector ~6%,
parse ~6%. `ncu` on the largest VV-merge launch: **not** DRAM bound (DRAM 43%,
compute 39%) -- it is latency bound (59% no-eligible-warp, 1.06 eligible warps at
89% occupancy) with **66% excess load sectors** (7.8/32 thread efficiency on the
child-stream loads).

### Negative result: warp-level expand merge (1 byte/lane)

Tried the direct `vpexpandb` analog: a warp owns one 32-output word, each lane
loads one coalesced child byte, `__ballot` builds the mask and `__shfl`
distributes the k-th dense byte by intra-word rank (`__popc(mask & lanemask_lt)`).
Fully coalesced loads and stores.

- Gotcha (fixed): the two `__shfl_sync(0xFFFFFFFF, ...)` for the left/right pick
  must run on the full converged warp; putting them in the data-dependent
  `if (bit)`/`else` deadlocks on Volta+ (each half waits for the other). Call both
  unconditionally, then select.
- Result: **json 27.8 vs ~51 GiB/s baseline -- ~2x slower.** The GPU has no
  single-instruction warp expand; 1 output/lane costs 2 `__shfl` + `__ballot` +
  `__popc` per output with no ILP, which dwarfs the coalescing win. The existing
  thread-per-byte merge already uses `__byte_perm` as a 4-output mini-expand per
  instruction at 8 bytes/lane -- far higher compute density. Reverted.

This matches the `extras/gpu` Metal microbench notes
(https://github.com/MarcinZukowski/pivco-huffman/tree/main/extras/gpu): their
`tree_merge_step` is the same 1-byte/lane `simd_prefix_sum`
gather, and their recorded verdict is "1 byte per GPU lane is too thin -- the
cross-lane prefix sum can't amortize; 4-bytes-per-lane vectorization is the
obvious next step." Our 8-byte/lane `byteMergeThread` is already that design; the
excess load sectors are L2 hits (intermediates stay L2-resident), so the true
limiter is per-group latency, not coalescing. Next levers to try: more MLP on the
merge (deeper unroll), cheaper rank, and cutting the O(depth) merge cascade.

### Wins (round 2)

Three committed wins after a fresh profile + a 5-thread literature search
(summarized in `IDEAS.md`):

1. **Directory `__launch_bounds__(256, 8)`.** The rank-directory kernel was
   register-limited (40 regs -> 6 blocks/SM, 63% occupancy) and compute+latency
   bound. Capping at 32 regs lifts it to 8 blocks/SM (85% occupancy). Directory
   kernel 522 -> 449 us (~14%).

2. **Multi-symbol per-thread flat unpack.** `scheduledFlatKernel<Depth>` decoded
   one symbol per thread (small depths re-read the same bitmap byte across
   threads). Now each thread loads the 8-symbol group's `Depth` bytes once,
   unpacks all eight `Depth`-bit indices from the register, and writes one
   coalesced 8-byte group (GPU analog of AVX512 `vpmultishiftqb`). flat1 on dna
   2.7x (943 -> 350 us).

3. **Aligned read-only child loads in the merge (the big one).** The merge read
   each child window with an unaligned 8-byte load; across a warp the rank-based
   cursors overlap/misalign, wasting ~66% of load sectors and leaving it latency
   bound (Long Scoreboard 66%). `dLoad8Aligned` issues two *aligned* 8-byte
   `__ldg` (read-only) loads of the enclosing 16-byte window + a funnel shift:
   aligned windows let neighbouring threads share L2/read-only sectors and the
   read-only path spares the L1 the rank loads use. VV merge -26%; it flips from
   latency bound (43% DRAM) to bandwidth-leaning (64% DRAM, 1.25 TB/s).

Combined decode gains (256 MiB, min-time vs round-1 baseline): deep-tree real
files +30-40% (json 51->68, log 52->73, cat-wiki 52->72, cat-image 76->116,
source_c 56->73, pride 52->68), skewed synthetics +30-68% (bell_s80 98->164,
proba02 50->78, english 74->104, proba14 61->84). dna 102->135 (flat+merge). Fast
tier unchanged. One regression: `calgary_pic` 119->111 (-7%) -- its small
cache-resident child streams make the merge compute bound, where the aligned
load's funnelshift costs more than the sector sharing saves (no clean per-node
gate: same node counts as json, only the global L2 working set differs).

**Negative results this round:** explicit K-way MLP in the merge (batch all ranks,
then all gathers, then all merges) -- K=2 ~1.5% (noise), K=4 regressed from
register-pressure occupancy loss; the dependent rank->gather chain caps it. New
profile (json): VV merge 42%, directory 21%, flat 14%, parse 7%, CV 6%. The merge
is now DRAM-bound with ~4x child-load over-read (byte_perm loads up to 8 from each
child = 2x, aligned doubles to 4x); intermediates exceed the 40 MB L2 so the
level-synchronous schedule thrashes to DRAM. Remaining levers in `IDEAS.md`.

### Wins (round 3): fuse the rank directory into the merge

4. **Directory-into-merge fusion (big win).** The standalone `scheduledDirectoryKernel`
   (~21% of decode) built every node's rank directory in *global* memory; each
   merge read it back, and -- because the two kernels are separate launches with
   all 8192 blocks processed between them -- the node bitmap was evicted from L2
   and re-read from DRAM by the merge. Each merge kernel now builds its own node's
   directory in *shared* memory (`buildSharedDirectory`) before merging, and the
   bottom-up dispatch drops the directory kernel. This removes the global directory
   write+read, serves the merge's rank from shared, and reuses the just-built
   bitmap (no redundant DRAM re-read). +40-92% across merge-heavy datasets, no
   regressions, and it recovered the `calgary_pic` aligned-load regression (now
   above its original baseline). The negative-result-earlier "fuse dir into merge
   is break-even" estimate was wrong: it missed the redundant-bitmap-DRAM-reread
   cost of the separate launch.

   Cumulative decode vs the round-1 baseline (256 MiB, median): json 51->84,
   log 52->88, pride 52->82, cat-wiki 52->87, chinese 65->94, csv 71->102,
   source_c 56->88, dna 102->167, calgary 119->138, cat-image 76->120, and skewed
   synthetics bell_s80 98->188, proba02 50->83, english 74->122, proba14 61->101
   -- roughly 1.4-1.9x on the previously-slow tier, fast tier unchanged. The
   top-down path keeps its standalone directory kernel (it reads the global
   directory in `topDownPartitionKernel`).

### Megakernel rejected by a size sweep

Before attempting the research's #1 idea (block-per-codec-block megakernel /
top-M shared fusion to keep intermediates on-chip), a working-set size sweep
settled whether the level-synchronous schedule is L2-thrash bound. json decode:
256blk 24, 512 46, 1024 61, 2048 78, 4096 77, 8192 84 GiB/s -- throughput
*rises* with block count and plateaus ~2048 blocks. If L2 thrash were the cost, a
smaller (L2-resident) working set would be faster per byte; instead it is slower
(under-occupied). The decode is parallelism/latency bound and parallelism-saturated
at full size. A megakernel trades away the block parallelism the workload needs --
exactly why both prior single-CTA attempts lost. Rejected. (This is the payoff of
the user's kernel-microbenchmark instinct: a cheap measurement killed a doomed
multi-hour rewrite.)

### Wins (round 4): parse readBits + constant/constant merge

5. **Byte-wise `readBits`.** The serial 1-thread parse read each node's numOnes
   field bit-by-bit; replaced with a byte-span load + shift/mask. Parse kernel
   580->452 us (~22%), decode +~2% across merge-heavy sets; also speeds the
   top-down path (shared parse kernel).

6. **Vectorized constant/constant merge.** `scheduledMergeConstantConstantKernel`
   decoded one output per thread with a per-bit read; now each thread loads one
   bitmap byte, selects left/right per bit from a 2-entry register table, and
   writes one coalesced 8-byte group (same shape as the flat unpack). CC kernel on
   csv 1.15M->459k ns (2.5x); decode csv 102->120 (+18%), and two-symbol
   distributions (root CC) 339->875/921 GiB/s (2.6-3.2x, now write-bound).

The vector/vector merge (60% of the deep-tree cost) now profiles as balanced --
58% compute, 60% memory, 62% L2 hit, 91% occupancy, latency bound on the child
loads -- i.e. near its floor: reducing compute or memory just exposes the other,
and directory coarsening was shown to *add* net popcount compute (the merge's
extra sub-word popcounts outweigh the halved build), so it is not pursued. Flat is
bandwidth bound on its coalesced output write; parse is at its serial floor.

### Win (round 5): default block size 64 KiB

7. **Reselect the default block size to 64 KiB.** A block-size sweep showed 16 KiB
   is slower than 32 KiB (2x the per-block overhead) and >32 KiB fell back to the
   generic decoder (~2 GiB/s) because the scheduled path capped at 32 KiB. Raising
   `kRankSelectMaxBlockSize` to 64 KiB (the merge's shared directory grows to
   ~8 KiB/CTA but occupancy stays 8 blocks/SM, register-limited) lets 64 KiB use
   the fast path, and 64 KiB halves the per-block overhead (parse, per-node
   directory builds, launches) at no merge cost -- the tree structure is fixed by
   the symbol distribution, not the block size, so a 64 KiB block has the same
   node count as a 32 KiB block but there are half as many blocks. Block count
   stays past the parallelism-saturation knee (~2048). 128 KiB was worse (shared
   bloat, launch issues, and 2048 blocks at the saturation edge).

   Default changed to 64 KiB in the bench. Decode +~15% *everywhere* with slightly
   better ratio, no real-file regression: json 85->101, log 89->105, pride 88->104,
   calgary 143->192, csv 120->144, dna 168->191, uniform 250->287, geometric
   150->182, bell_s10 101->124, proba02 84->100.

### Round 6: instruction / redundant-code reduction

Directive: reduce the number of instructions -- remove redundant/inefficient
code, move edge cases out of the hot loop. Findings (the decode is memory-latency
bound: the VV merge issues only 0.45 warps/scheduler/cycle -- ~55% idle issue
slots -- with 74% memory-scoreboard stalls, so total-instruction cuts on the
parallel kernels are perf-neutral; they help only where issue/serial bound):

- **Directory-build zeroing removal (win, +1-2%).** `buildRankSelectDirectoryCooperative`
  zeroed all of prefixA[0..words] before the popcount loop overwrote [1..words];
  only prefixA[0] needs it. Removed the O(words) redundant-write pass and its
  barrier. json 101->103, dna 191->194, calgary 191->195.
- **Skip dead numOnes in the fused build (neutral, dead-code).** `*ones` (a
  corruption check) is never read by the merge; templated the builder on
  `ComputeOnes` and pass false from the merge, dropping the read + 2 barriers.
  Correct, perf-neutral (those barriers were cheap).
- **Drop merge tail masking (neutral, cleaner).** The final partial group's high
  bits only route over-stored outputs (into padding/slop); removed the per-group
  `n=min(8,...)` + mask from the hot loop. Perf-neutral.
- **Negative: `dLoadLe32Masked` single-load fast path.** Replacing the
  byte-by-byte bounds loop with one unaligned `memcpy` + a branch *regressed* VV
  -13% (json 103->93). The compiler already vectorizes the L1-hot byte loop; the
  unaligned load + branch is worse. Reverted.

Takeaway: instruction reduction helps the *serial/issue-bound* stages (parse
`readBits` +22%, directory-build zeroing +1-2%) but not the memory-latency-bound
parallel merges (idle issue slots), and hand "optimizing" the compiler's hot
loops can regress. The decode remains at its memory-latency floor.

### Round 7: breaking the child-load latency (research + radix-4)

Directive: break the ~200-cycle dependent child-load. Two more research passes
(3 agents) + implementation:
- **Concurrency floor confirmed (Little's Law).** At the 64-warp/SM ceiling with
  MLP=2 (register-capped) and fixed L2 latency, `bytes_in_flight` is pinned; the
  merge issues only 0.45 warps/scheduler/cycle (~55% idle issue slots), 74%
  memory-scoreboard stalls. The `byte_perm` LUT expand is already the SotA GPU
  expand (no `vpexpandb`/`PDEP` on NVIDIA); the merge is wavelet-tree
  reconstruction. Neutral/negative: `prefetch.global.L2` ahead (elided / already
  L2-warm), explicit depth-2 software pipeline (unroll=2 already = MLP=2),
  `__ldcg` (loses read-only-cache dedup), cp.async streaming (occupancy),
  warp-specialization (parallelism-saturated).
- **Win: flat kernel depth-2 MLP.** The flat kernels had no unroll (MLP=1) and are
  latency bound on the packed-index load; fetching 2 groups before unpacking gives
  MLP=2. +1-2% on flat-heavy sets (csv, proba14, chinese, english). (MLP=4 was
  neutral -- reverted.)
- **Directory build zeroing removal** (round 6, +1-2%) and dead-numOnes / tail-mask
  removals (neutral cleanups).
- **REJECTED (measured): radix-4 level fusion.** Fuse a node + 2 internal children,
  gather from the 4 grandchildren, skip child materialization. Root-scoped
  (buffer-safe) measured json 65 vs 103 GiB/s: the fused child level is at an
  UNALIGNED position (childPos = node rank), forcing slow bit-level rank
  (`dRankSelectOnesBefore`) + `dGetBits`, plus extra directory builds, which
  overwhelm the ~1/3 intermediate-traffic savings. The general version also breaks
  the 2-buffer ping-pong (L reads L+2, same parity). This was the one structural
  lever to cut the O(depth) load-levels; it does not pay off. The decode is at its
  A100 concurrency floor.

### Session cumulative

vs the round-1 baseline (32 KiB), ~2x on the previously-slow tier: json 51->101,
log 52->105, pride 52->104, cat-wiki 52->~104, source_c 56->111, csv 71->144, dna
102->191, chinese 65->112; skewed synthetics ~2x (proba02 50->100, bell_s30
51->106, english 74->155); two-symbol 2.6-3.2x (335->928). Fast/flat tier
unchanged. Seven committed wins (flat unpack, aligned merge loads, directory
launch_bounds, directory-into-merge fusion, byte-wise readBits, CC vectorization,
64 KiB blocks); megakernel/level-fusion rejected by a size sweep (the workload is
parallelism-saturated, not L2-thrash bound). The vector/vector merge is now
balanced/near-floor; further multiples would need a different algorithm (top-down
was ~2x slower).

## Round 8 -- shared-memory staging of the merge inputs (cp.async) -- REJECTED

Directive: stage the vec/vec & cst/vec merge child inputs in shared memory to fix
the "each thread loads from a different region" (scattered DRAM gather) pattern.
Pursued exhaustively (two research sub-agents, profile-driven), then reverted as a
net regression. Full A/B (real datasets, GiB/s), staged vs committed baseline:

| dataset | baseline | staged (final) | ratio |
|---|---|---|---|
| dna | 190.6 | 174.0 | 0.91 |
| calgary | 191.6 | 171.3 | 0.89 |
| json | 101.1 | 88.4 | 0.87 |
| log | 104.5 | 90.9 | 0.87 |
| csv | 144.3 | ~150 | 1.04 (only win) |

Design (all correct, verified): rank is monotonic, so a contiguous output chunk
reads a contiguous slice of each child. Chunk the node (2048 outputs); cp.async
double-buffer each child's slice global->shared (coalesced) overlapped with the
prior chunk's merge; merge with the compute-light `byteMerge8` from shared, folding
the chunk's child base into the shared pointer so the per-output cursor math equals
the non-staged path. Iterated: naive stage (dna 109) -> warp-shuffle expand (55-71,
too ALU-heavy) -> transpose STG.64 stores -> cp.async double-buffer + byteMerge8
(147) -> buffer-pointer consolidation 40->32 regs => 6->8 blocks/SM, 92% occ (170)
-> geom carry (174). `__launch_bounds__` spills (hurts); chunk 2048 optimal.

**Why it loses (profiled, definitive):**
- Baseline big VV: DRAM 63% / ALU 52% / 481us -- **DRAM-latency-bound** on the
  scattered gather (23.8-cyc memory-scoreboard stall), ALU has slack. `dLoad8Aligned`
  (aligned `__ldg` + funnelshift) already dedups sectors, so it moves ~min DRAM.
- Staged big VV: coalescing worked (moves the load off the critical path) but the
  kernel becomes **ALU-bound at 69% / DRAM 54% / 569us**. It executes ~1.5x the
  baseline's ALU (96M vs ~61M). Attacked that ALU three ways -- fold child base into
  the pointer (no change; compiler already CSE'd it), byte-aligned chunk-rank (worse),
  carry geometry to halve rank lookups (+1-2%) -- the ~1.5x gap resists reduction.
- Net: coalescing the load cannot speed up a merge whose real limit, once coalesced,
  is per-output integer work; the extra staging ALU exceeds the DRAM saving.

Lesson: the scattered child gather is NOT wasting DRAM (the aligned read-only load
already coalesces it); the merges are latency-bound at the A100 concurrency floor,
and the true ceiling is per-output ALU (rank + byteMerge8 + window extraction),
which staging adds to rather than removes. Reverted to the baseline merges. The
cp.async staged implementation is preserved in IDEAS.md as the best-known staged
design (a foundation if the per-output ALU is ever cut, e.g. a cheaper rank/expand).

## Round 9 -- chunked top-down decode entirely in shared memory (IN PROGRESS, behind PIVCO_CHUNK_TD)

New single-kernel decoder: a warp owns one contiguous CHUNK (S=1024 outputs) of a
block and decodes the WHOLE tree for that chunk with all intermediate node streams
resident in shared -- only bitmaps/flat-indices read from L2, output written once.
Enabled with PIVCO_CHUNK_TD=1 (default path unchanged). Dispatch: parse ->
directory (per-node rank dirs for the descent) -> pivcoChunkTopDownKernel.

Structure (3 research rounds informed it): Phase 0 builds per-level node lists in
shared (CTA-wide). Phase 1 = warp-parallel top-down descent (levels serial, nodes
within a level 32-wide; two boundary ranks/node from the L2 directory give each
child's contiguous chunk sub-range; streamOff via warp scan). Phase 2 = bottom-up
merge (deepest level first) in the 2*S ping-pong shared buffers, byteMerge8 with a
warp-scan group rank (no per-group directory read). Phase 3 = coalesced flush.

Correctness: verified on all 11 real datasets (compute-sanitizer caught & fixed a
CC-children uninitialized-subLen OOB). Progress (dna / json / calgary / log GiB/s):
lane-0 serial descent + 1-out/lane merge 42/18/24/19 -> warp-parallel descent +
per-level lists + flat 2-byte window 56/28/43/29 -> byteMerge8 + warp-scan rank
64/30/42/31.

Profiling (json): **DRAM 4.2%** -- the design goal (near-zero global traffic,
intermediates in shared) is ACHIEVED (baseline merge is 63% DRAM). Now
COMPUTE-bound (SM 71%, ALU highest) and occupancy-limited (48%, register+shared cap
at 4 blocks/SM). The dna(64)/json(30) split is the small-node pathology: many-symbol
trees give each node a tiny per-chunk sub-range, so per-node warp overhead dominates.

Still ~3-4x below baseline (193/103/194/106). Next levers (identified, not yet done):
(1) load-balancing search per level (process a level's concatenated outputs 32-wide
with node lookup, moderngpu MergePath) to kill the small-node overhead -- the top
remaining win for json/calgary/log; (2) occupancy (cut registers/shared to reach 8
blocks); (3) aligned uint64 store via 8-byte-padded streamOff. The design has
cleared its highest-risk gate (collapsing the cascade to shared DOES drop DRAM to
~4%); the remaining gap is compute/occupancy, not memory.

### Round 9 continued -- chunk-TD optimizations + fundamental ceiling analysis

Optimizations landed (all behind PIVCO_CHUNK_TD, correct on all 11 datasets;
GiB/s at 268 MiB, dna / json / calgary / log):
- warp-parallel descent + per-level node lists + flat 2-byte window: 56 / 28 / 43 / 29
- byteMerge8 + warp-scan group rank: 64 / 30 / 42 / 31
- __launch_bounds__(256,6) (occupancy 48%->72%): +occupancy
- node-per-lane merge for >=8-node levels (32 small nodes in parallel): dna big win
- loop-free descent rank (dRankOnesBeforeFast): json 32->35, calgary 46->49
- aligned uint64 stores on 8-byte-padded few-node levels: json 35->38, calgary 49->52, dna ->75

Current chunk-TD: dna 75, json 38, calgary 52, log 38 vs baseline 193/103/194/106.
Profile (json): DRAM 5.5% (design goal met), compute-bound SM 78% / ALU 61%,
occupancy 72%.

**Fundamental ceiling (research round 3, confirmed):** PivCo's partition tree IS a
(truncated) wavelet tree; reconstructing the root sequence from the per-node bitmaps
is inherently Theta(n * avg-leaf-depth) = Theta(n log sigma) ALU -- every output is
touched once per tree level on its root->leaf path. The BASELINE cascade does the
SAME total ALU but is DRAM-latency-bound at 64 warps/SM, so that ALU runs for free
in the shadow of memory stalls. The chunk-in-shared decoder deleted the DRAM
traffic (63%->~5%) and thereby deleted the thing that was hiding the ALU, so the
same work is now fully exposed and it is ~2-3x slower on deep (many-symbol) trees.
To beat the baseline the chunk decoder must cut ALU ~3.2x (json) / ~2.2x (dna).

Ruled out (research): table-driven multi-symbol decode is INFEASIBLE for this format
-- symbol i's codeword bits are scattered one-per-level across the bitmaps at
rank-dependent positions; there is no contiguous codeword stream to index, and
assembling one IS the O(depth) walk. cp.async double-buffer / megakernel / plain
shared-staging are all measured losses (the baseline's latency-hidden cascade is
already the "overlap ALU behind idle memory" design). The one remaining decode-only
lever is radix-K level fusion (cut the depth factor); the biggest lever overall
(shallower trees / wider FLAT leaves) is an ENCODER change, out of scope here.

Net: the chunk-TD is a correct, well-optimized realization of "decode entirely in
shared" that achieves its stated goal (near-zero intermediate global traffic), but
the workload is compute-bound once memory is removed, so it does not beat the
DRAM-latency-hidden baseline on this GPU. It would win only where the baseline is
DRAM-BANDWIDTH bound (not the case at 58% DRAM-SOL / latency-bound today).

### Round 9 result -- chunk-TD wins for SMALL inputs; now auto-dispatched

Key measured finding: although the chunk-in-shared decoder is ~2.5x slower than the
bottom-up cascade at large sizes (compute/wavelet-tree-ALU bound), it is FASTER for
small inputs because it runs only 3 kernel launches (parse, directory, chunk) vs the
cascade's ~30 per-level launches, whose fixed ~5us/launch overhead dominates small
decodes. Measured baseline vs chunk-TD (GiB/s):
- 1 MiB:  json 3.6->8.4 (2.3x), calgary 5.0->9.1 (1.8x)
- 4 MiB:  json 17->24 (1.4x), calgary 19->28, dna 45->54, log 28->33
- 8 MiB:  all datasets still favor chunk-TD (~1.2x)
- >=12 MiB: cascade pulls ahead (steady-state throughput wins)

So decode is now **size-adaptive**: dstSize <= 8 MiB uses the chunk-TD kernel,
larger uses the bottom-up cascade (unchanged). `PIVCO_CHUNK_TD=1` forces chunk-TD at
any size; `PIVCO_CHUNK_TD=0` forces the cascade. This makes small-payload decode
1.2-2.3x faster with zero large-input regression -- a real payoff from the top-down
direction, distinct from (and orthogonal to) the large-input cascade work. Correct
on all 11 datasets at small and large sizes.

### Round 10 -- large-input chunk-TD is at the occupancy/issue floor (levers exhausted)

Attacked the large-input chunk-TD again, driven by the ncu profile (compute/issue
bound: SM 78%, ALU 61%, IPC 3.11/4, issue-slots ~78%, DRAM ~5%, 40 regs, 72%
occupancy). Since the bottleneck is instruction ISSUE, the only real lever is fewer
dynamic instructions per output byte. Went through them:

- **Skip constant-child materialization** (CV/VC): landed (b83ce5421dd0). A CV/VC
  merge emits its constant side from leftSym/rightSym and never reads that child's
  stream, so zeroing the const child's subLen skips the fill. calgary 52->54, log
  38->39 GiB/s @256MB, neutral elsewhere. Small clean win.

- **Aligned uint64 stores on many-node (node-per-lane) levels: MEASURED -24%,
  reverted.** `mergeNodeSingleLane`'s internal branch byte-stores (up to 8 STS.U8
  per group) because node-per-lane buffers are byte-packed, not 8-aligned. The
  few-node levels already 8-byte-pad for aligned stores (that was the +8.5% json
  35->38 win). Extending padding+aligned-stores to levels with 8..31 nodes
  (`kChunkTdPadThreshold=32`, templated `mergeNodeSingleLane<Aligned>`) cut the
  store instruction count but grew the per-warp buffer by ~224 B, which crossed an
  occupancy cliff (6->5 blocks). Same-size A/B @64MB forced chunk-TD:
  json 37.1->28.3, calgary 63.2->48.1, dna 89.9->70.8, log 46.6->35.1 -- a uniform
  ~-24%. The occupancy loss dwarfs the instruction savings. KEY INSIGHT: the kernel
  is NOT purely issue-bound; its ~22% stall cycles are hidden by occupancy, so ANY
  instruction cut that costs shared memory is net-negative. Padding ALL levels (for
  full alignment) would ~2x the buffer (leaf level has up to ~256 nodes) and halve
  occupancy -- strictly worse.

- **byteMerge8 / dLoad8Shared: at the instruction floor.** byteMerge8 = 2
  __byte_perm (minimal: each emits 4 of the 8 bytes) + 2 LUT loads; dLoad8Shared
  already skips the 2nd shared load on the aligned path. No cheaper form.

- **kMergeSel -> __constant__: rejected on analysis.** The LUT index (mask nibble)
  is data-divergent per lane; constant memory only broadcasts on uniform access and
  serializes divergent access, so it would be SLOWER than the current __device__
  const (L1/L2-cached, parallel).

Net for Round 10: the two shared-neutral instruction cuts are both measured losses
(radix-4 -37% in ROOFLINE.md; aligned-store -24% here), and the kernel sits on an
occupancy cliff so shared-costing cuts backfire. **The large-input chunk-TD is at
its floor.** The bottom-up cascade remains the better large-input decoder (it hides
the same wavelet-tree ALU behind DRAM latency; see Round 9), and chunk-TD stays the
size-adaptive winner for small inputs (<=8 MiB). No decode-only lever beats the
baseline on large inputs on this GPU; the only structural win left is an ENCODER
change (shallower trees / wider FLAT leaves / quad-vector format), which is out of
scope.

**Latent issue discovered (pre-existing, not from this round):** compute-sanitizer
on the forced chunk-TD path flags a 1-byte `__global__` over-read on the LAST block
(`dRankOnesBeforeFast`/`bm[bi+1]`/`slice[bi+1]` read up to a few bytes past the
bitstream, by design "into the trailing slop, all masked off"). For interior blocks
the slop is the next block's bytes; the last block relies on `src` having trailing
slop. Verified pre-existing (reproduces with the change reverted). Harmless in
deployment (cudaMalloc rounds allocations up, and the bytes are masked off) but
technically OOB when `src` is exactly sized. The bottom-up rank helpers share the
pattern. Left as-is (fixing it means defining/padding a src-slop contract, orthogonal
to decode perf); recorded here so a future robustness pass can add trailing src slop.

### Round 11 -- WIN: size-adaptive chunk width (the chunk size was under-tuned)

The occupancy-cliff insight from Round 10 (the kernel's stall cycles are hidden
by occupancy, so shared-costing instruction cuts backfire) pointed at a lever I
had NOT tried: change the CHUNK SIZE. Each per-chunk tree descent (Phase 0/1) is
fixed overhead paid once per chunk; a wider chunk amortizes it over more outputs.
Swept kChunkTdOutputs @64 MiB forced chunk-TD:

- 512:  json 27, calgary 39, dna 59, log 30  (per-chunk overhead dominates)
- 1024: json 37, calgary 63, dna 90, log 47  (the shipped value -- UNDER-TUNED)
- 2048: json 44, calgary 76, dna 102, log 54 (+13-21%, dna breaks 100 GiB/s)
- 4096: json 38, calgary 71, dna 88, log 47  (overshoots; buffer/occupancy loss)

2048 is the optimum for large inputs. But the optima are OPPOSITE by size: tiny
inputs want the small chunk (its extra chunks give the grid enough parallelism to
fill the GPU -- at 4 MiB dna 1024=54 vs 2048=40), large inputs want the wide chunk
(amortization). So the chunk size is now chosen at dispatch by input size and
passed to the kernel at RUNTIME (S and the per-warp buffer are runtime; the shared
budget is set per launch). No templating needed -- S only appears in arithmetic.

The wider chunk lifts the chunk decoder's steady-state enough to push the
crossover with the bottom-up cascade from ~8 MiB out to ~12 MiB. Measured
crossover (same-run A/B): auto wins at 12 MiB for every dataset (+4..+27%); at
16 MiB it is mixed (dna +14% but calgary -7% -- deep 256-symbol trees cross
earliest). So the flat auto threshold is 12 MiB.

Shape gate for the one exception: gzip_random.gz (incompressible, shallow tree,
tableLog 8) has a fast baseline (few merge levels -> few launches) and crosses at
~5-6 MiB, so a flat 12 MiB threshold regressed it -5%. Gating the wide auto window
on tree depth fixes it cleanly: deep trees (tableLog >= 10) use chunk-TD to
12 MiB, shallow trees cap at 4 MiB (where gzip still ties). tableLog cleanly
separates the datasets (gzip=8; everything else 10-12).

Final shipped result (A100, min-time GiB/s, decode only; no regression anywhere):
- 4 MiB:  +41..+79% across all 11 real datasets (gzip +3%).
- 10 MiB: +15..+38% across all 11 real datasets (gzip +0%, uses baseline).
- >12 MiB: unchanged (baseline; chunk-TD still loses there per Rounds 9-10).

Tests 11/11; compute-sanitizer clean on the 2048 path. Committed b2bb5b0278cb.
This is the payoff from the top-down direction on medium inputs: the 8-12 MiB
regime was baseline-only before and is now chunk-TD, and the whole <=12 MiB range
is faster. Note the large-input (>12 MiB) decode is still the cascade -- the
Round 10 floor stands there; the win is that the chunk decoder's own ceiling rose
enough to extend its regime.

## Round 12 (2026-07-20) -- WIN: vectorized the flat-root fast path

Every prior round optimized the scheduled-cascade slow tier and treated the two
`fastMode` paths as done. Re-profiling from scratch (taking the docs "with a grain
of salt") showed one of them was ~4x below its own roofline.

`fastDecodeFlatRootKernel` handles the case where the whole tree is a single flat
leaf: uniform / incompressible / small-alphabet data (uniform, gzip_random,
sparse_4/16, flat_M3/5/6/7). It was decoding ONE symbol per thread -- a dependent
1-2 byte load, a bank-conflicted 256-entry shared gather, and a byte store per
output. This is exactly the shape `scheduledFlatKernel` had already been optimized
away from (Round 2's "multi-symbol per-thread flat unpack"), but the fast-root twin
never got the same treatment.

`ncu` on `uniform` (depth-8 flat root, 128 MiB): DRAM 23.2% / compute 21.1% / mem
24.5% SOL, 4.48M shared bank conflicts, 574 us. Neither pipe near its roof --
purely latency-bound on the per-symbol dependent load + conflicted gather. At
246 GiB/s it *looked* fast next to the 100 GiB/s deep-tree tier, but its own DRAM
roofline (2 B/out) is ~930 GiB/s.

Fix: rewrote the kernel to the scheduled-flat shape -- 8 outputs per thread, one
packed load of the group's `depth` bytes via `dLoad8Bounded`, unpack the eight
`depth`-bit indices from the register, one shared-table lookup each, one coalesced
8-byte store, with depth-2 MLP (two groups' loads issued before either unpack). A
single unified path covers depths 1-8 (depth 8 -> mask 0xFF, each packed byte is
one index). `out = dst + blockOff` is 8-byte aligned (blockOff is a multiple of the
block size) with `PIVCO_GPU_DECODE_DST_SLOP` trailing slop, so the final group's
over-store is harmless -- same store contract as the merge kernels.

Result (256 MiB, 15 iters, decode_median_GiBps), vs the round-1 baseline:

| dataset | before | after | delta |
|---|---|---|---|
| sparse_4 | 280.3 | 884.6 | +216% |
| flat_M3 | 233.0 | 827.6 | +255% |
| sparse_16 | 272.8 | 790.1 | +190% |
| flat_M5 | 268.9 | 718.1 | +167% |
| flat_M6 | 263.4 | 682.0 | +159% |
| gzip_random.gz | 244.6 | 654.5 | +168% |
| flat_M7 | 262.8 | 654.5 | +149% |
| uniform | 246.6 | 622.8 | +153% |

Every other dataset (which takes the cascade / three-symbol paths) is within
+-0.5%. Aggregate over all 30 datasets: geomean 189.2 -> 248.9 GiB/s (+31.5%),
arithmetic mean 232.5 -> 357.7 GiB/s (+53.9%). No regressions. Byte-identical
output; GPU tests 11/11; partial-final-block over-store verified against a
non-block-aligned size (`--size=33554321`).

TRIED-NO-WIN this round (reverted, both in the ~+-12% per-build code-layout noise
with real regressions on some datasets):
- Lowering `kBottomUpChunkedMergeThreshold` (vectorize small merge nodes): helped
  calgary/csv/zipfian but regressed chinese_text -10% (mid-size nodes prefer the
  scalar path's full-CTA occupancy over byteMerge's thread underutilization); the
  scalar/vector crossover is data-dependent, so no single threshold wins.
- `dLoadLe32Masked` single-load fast path for the common interior word: mixed
  bidirectional swings (source_c/dna +11-13%, chinese/csv -12%) that tracked the
  build, not the change.

Independent `ncu` re-confirmed the deep-tree slow tier is unmoved and at its floor:
the VV merge (~68% of json decode) is latency-bound, not throughput-bound -- across
its per-level launches compute peaks ~71%, DRAM peaks ~64%, neither saturates,
CPI ~19-30 at ~78% occupancy with warps maxed (64/SM). Consistent with the prior
rounds' concurrency-floor conclusion; a further multiple there needs an algorithm
or wire-format change, not a kernel tweak.

Lesson: audit the "already fast" fast paths against their OWN roofline, not against
the slow tier -- 250 GiB/s was 4x under ceiling.
