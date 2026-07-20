# PivCo-Huffman GPU Decode Benchmarks

Device: **NVIDIA A100 (SM 8.0)**. Benchmark: 256 MiB expanded
(`--size=268435456`), 8192 blocks of 32 KiB, decode timed with CUDA events
(H2D/D2H excluded). Run-to-run GPU-clock variance is ~±15%; treat
`decode_min_time_GiBps` as the more stable figure and always re-measure A/B
back-to-back. Path is the production bottom-up scheduled merge decoder.

Command (the real datasets come from the upstream pivco-huffman repo's
`extras/datasets`, https://github.com/MarcinZukowski/pivco-huffman — clone it and
point `--dataset-dir` at that directory):

```bash
git clone https://github.com/MarcinZukowski/pivco-huffman
buck2 build @fbcode//mode/opt fbcode//openzl/dev/contrib/pivco-huffman/gpu:pivco_gpu_bench
<BIN> --size=268435456 --iterations=15 --dataset-dir=pivco-huffman/extras/datasets
```

## 2026-07-20 — vectorized flat-root fast path (256 MiB, 15 iters)

`fastDecodeFlatRootKernel` (used whenever the whole tree is a single flat leaf:
uniform / incompressible / small-alphabet data) was left decoding one symbol per
thread — a dependent 1-2 byte load, a bank-conflicted 256-entry shared gather,
and a byte store per output. `ncu` on `uniform` measured it at DRAM 23% / compute
21% / memory 24% SOL with 4.5M shared bank conflicts, i.e. ~4x below its roofline
and purely latency-bound. Rewrote it to the same shape the scheduled flat kernel
already used: 8 outputs per thread, one packed load, unpack the eight indices from
a register, one coalesced 8-byte store, with depth-2 MLP. Byte-identical output;
all 11 GPU unit tests pass; partial-final-block over-store verified against a
non-block-aligned size.

`decode_median_GiBps`, delta vs the round-1 baseline that the table below uses.

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

Every other dataset is within ±0.5% (a different code path). Aggregate over all
30 datasets: geomean 189.2 -> 248.9 GiB/s (+31.5%), arithmetic mean 232.5 -> 357.7
GiB/s (+53.9%), no regressions.

The deep-tree scheduled cascade (the ~100-115 GiB/s slow tier: json, log, pride,
source_c, cat-wiki, proba02, zipfian, bell_s30) is unchanged and remains at its
architectural floor: independent `ncu` profiling confirms the vector/vector merge
that dominates it (~68% of json decode) is latency-bound at max occupancy (256
threads x 8 blocks/SM = 64 warps/SM, the A100 ceiling), with both the compute and
DRAM pipes below their roofs and per-thread MLP register-capped. Its levers
(radix-4 fusion, megakernel, cp.async staging) were all measured net-negative in
prior rounds; small inner-loop tweaks land inside the ~±12% per-build code-layout
noise.

## Current — 2026-07-18 after 7 wins, default block size now 64 KiB (256 MiB, 15 iters)

`decode_median_GiBps`, delta vs the round-1 baseline (which was at 32 KiB blocks).
Committed wins: per-thread 8-symbol flat unpack, aligned read-only (`__ldg`) merge
child loads, directory `__launch_bounds__`, fusing the rank-directory build into
the merge kernels (shared memory), byte-wise `readBits` in the parse, vectorizing
the constant/constant merge, and reselecting the default block size to 64 KiB
(scheduled fast path now supports it; ~+15% everywhere, slightly better ratio).

### Real datasets

| dataset | ratio | decode GiB/s | vs base |
|---|---|---|---|
| gzip_random.gz | 1.000 | 285.9 | ~flat |
| calgary_pic | 0.209 | 191.6 | +61% |
| dna_fasta.fa | 0.283 | 190.6 | +86% |
| csv_numeric.csv | 0.418 | 144.3 | +104% |
| chinese_text.txt | 0.731 | 112.5 | +74% |
| source_c.c | 0.623 | 111.1 | +99% |
| log_apache.log | 0.692 | 104.5 | +102% |
| pride.txt | 0.573 | 104.3 | +99% |
| json_api.json | 0.655 | 101.1 | +97% |

### Synthetic datasets

| dataset | ratio | decode GiB/s | vs base |
|---|---|---|---|
| two_sym_90/10 | 0.125 | 928.3 | +226% |
| two_sym_eq | 0.125 | 884.6 | +164% |
| proba80 | 0.156 | 401.5 | +56% |
| uniform | 1.000 | 286.9 | +15% |
| sparse_4/16 | - | 273-280 | ~flat |
| flat_M3..M7 | - | 263-276 | ~flat (fast path) |
| proba50 | 0.250 | 223.0 | +65% |
| bell_s80 | 0.995 | 190.6 | +95% |
| geometric | 0.265 | 181.8 | +75% |
| english | 0.531 | 154.8 | +109% |
| bell_s10 | 0.685 | 123.7 | +111% |
| proba14 | 0.527 | 118.6 | +94% |
| zipfian | 0.783 | 111.1 | +94% |
| bell_s30 | 0.877 | 105.6 | +106% |
| proba02 | 0.891 | 99.8 | +101% |

**Roughly 2x on the previously-slow tier vs the round-1 baseline** (json 51->101,
log 52->105, pride 52->104, csv 71->144, dna 102->191, source_c 56->111), 2.6-3.2x
on two-symbol data, and every real file improved with no regression. Fast/flat
tier ~flat (flat_M* -3% is clock noise). The remaining slow tier (proba02/bell_s30
~100-106) is bounded by the vector/vector merge, which profiles balanced (58%
compute, 60% memory, 91% occupancy) -- near its per-kernel floor.

## Round-1 baseline — 2026-07-18 (256 MiB, 15 iters), `decode_median_GiBps`

### Real datasets

| dataset | ratio | decode GiB/s |
|---|---|---|
| gzip_random.gz | 1.000 | 289.3 |
| calgary_pic | 0.209 | 118.8 |
| dna_fasta.fa | 0.283 | 102.3 |
| cat-image.jpg | 0.990 | 76.2 |
| csv_numeric.csv | 0.418 | 70.6 |
| chinese_text.txt | 0.732 | 64.7 |
| source_c.c | 0.624 | 55.9 |
| pride.txt | 0.573 | 52.4 |
| log_apache.log | 0.692 | 51.8 |
| cat-wiki.html | 0.692 | 51.7 |
| json_api.json | 0.655 | 51.2 (min-time 59.5; noisy) |

### Synthetic datasets

| dataset | ratio | decode GiB/s |
|---|---|---|
| two_sym_eq | 0.125 | 335.4 |
| sparse_4 | 0.250 | 286.9 |
| two_sym_90/10 | 0.125 | 285.2 |
| flat_M3 | 0.375 | 282.2 |
| sparse_16 | 0.500 | 280.9 |
| flat_M5 | 0.625 | 274.3 |
| flat_M6 | 0.750 | 271.0 |
| flat_M7 | 0.875 | 269.5 |
| proba80 | 0.157 | 257.8 |
| uniform | 1.000 | 248.4 |
| proba50 | 0.250 | 134.7 |
| geometric | 0.266 | 103.6 |
| bell_s80 | 0.995 | 97.7 |
| english | 0.531 | 73.9 |
| proba14 | 0.527 | 61.1 |
| bell_s10 | 0.686 | 58.6 |
| zipfian | 0.783 | 57.3 (min-time 63.1; noisy) |
| bell_s30 | 0.878 | 51.2 |
| proba02 | 0.891 | 49.6 |

### Observations

- **Fast tier (~250-335 GiB/s):** flat-root / shallow / highly-skewed
  distributions — decode is dominated by one coalesced output write.
- **Slow tier (~50-75 GiB/s):** deep Huffman trees (most real text/binary:
  json, log, pride, cat-wiki, source_c, chinese; synthetics proba02/14,
  bell_s10/30). Dominated by the per-level vector/vector merge cascade.
- Working hypothesis (verify by profiling): the slow tier is limited by the
  O(tree-depth) global-memory round-trips of the ping-pong intermediate streams.
  The AVX512 CPU path keeps that ping-pong in cache; the GPU path stages it in
  global memory.

## Size-adaptive dispatch: chunk-TD decoder for small inputs (round 9)

The chunk-in-shared top-down decoder (PIVCO_CHUNK_TD) runs 3 kernel launches vs the
bottom-up cascade's ~30. For small inputs the cascade's fixed per-launch overhead
dominates, so decode is now size-adaptive: dstSize <= 8 MiB uses the chunk decoder,
larger uses the cascade (unchanged). Measured @ 4 MiB, baseline vs chunk-TD (GiB/s):

| dataset | baseline | chunk-TD | speedup |
| calgary_pic | 18.4 | 27.6 | 1.50x |
| cat-image.jpg | 27.1 | 35.3 | 1.31x |
| cat-wiki.html | 15.9 | 24.1 | 1.52x |
| chinese_text.txt | 19.2 | 27.4 | 1.43x |
| csv_numeric.csv | 20.6 | 29.8 | 1.45x |
| dna_fasta.fa | 25.6 | 36.7 | 1.43x |
| json_api.json | 16.9 | 24.5 | 1.45x |
| log_apache.log | 15.6 | 23.1 | 1.48x |
| pride.txt | 17.0 | 25.4 | 1.50x |
| source_c.c | 17.7 | 25.3 | 1.43x |

At 1 MiB the win is larger (json 3.6->8.4 = 2.3x, calgary 5.0->9.1 = 1.8x). Above
~8-12 MiB the cascade's steady-state throughput wins (chunk-TD is wavelet-tree
ALU-bound once its DRAM traffic is ~0). Large-input numbers unchanged.

## Round 11: size-adaptive chunk width extends the win to ~12 MiB

The chunk decoder's chunk width is now chosen at dispatch (runtime kernel arg):
1024 outputs for tiny inputs (more chunks -> more grid parallelism), 2048 above
6 MiB (each per-chunk tree descent is fixed overhead, so a wider chunk amortizes
it). The wider chunk raises the decoder's steady-state ~15-20%, pushing the
crossover with the cascade from ~8 MiB out to ~12 MiB. The auto threshold is
gated on tree depth (deep trees tableLog>=10: chunk-TD to 12 MiB; shallow trees
like an incompressible .gz: 4 MiB, since their baseline is already fast).

Measured (A100, min-time GiB/s, decode only), baseline vs auto, no regression:

| dataset | 4 MiB base | 4 MiB auto | 10 MiB base | 10 MiB auto |
| calgary_pic | 15.9 | 28.5 (+79%) | 45.2 | 52.1 (+15%) |
| cat-image.jpg | 27.4 | 38.9 (+41%) | 50.2 | 60.7 (+21%) |
| cat-wiki.html | 16.0 | 24.8 (+54%) | 30.8 | 41.8 (+35%) |
| chinese_text.txt | 19.3 | 27.6 (+43%) | 35.9 | 46.3 (+29%) |
| csv_numeric.csv | 21.1 | 29.8 (+41%) | 40.9 | 51.6 (+25%) |
| dna_fasta.fa | 26.5 | 39.3 (+48%) | 54.2 | 70.1 (+29%) |
| gzip_random.gz | 57.8 | 59.6 (+3%) | 151.4 | 151.4 (+0%) |
| json_api.json | 16.9 | 25.3 (+49%) | 32.7 | 43.0 (+31%) |
| log_apache.log | 15.9 | 24.0 (+50%) | 32.9 | 43.7 (+33%) |
| pride.txt | 16.7 | 26.1 (+56%) | 32.5 | 45.2 (+38%) |
| source_c.c | 17.6 | 25.8 (+46%) | 34.1 | 45.0 (+32%) |

The 8-12 MiB regime was baseline-only before round 11; it is now chunk-TD. Above
~12 MiB the cascade still wins (chunk-TD is wavelet-tree ALU-bound; see the round
10 floor). Forced chunk-TD @64 MiB improved too (json 37->44, calgary 63->76, dna
90->102, log 47->54 GiB/s) but the cascade remains faster at that size.

## Bottom-up vs top-down decoder, head-to-head (forced)

Direct comparison of the two decode kernels on every real dataset, forcing each
path (`PIVCO_CHUNK_TD=0` = bottom-up cascade, `PIVCO_CHUNK_TD=1` = top-down
chunk-in-shared) regardless of the auto-dispatch. Decode-only median GiB/s,
A100, 64 KiB blocks, DCGM profiling muted (`sudo dyno dcgm_profiling
--mute=true`) for stable clocks, 25 iterations. The shipped decoder auto-selects
the winner per input size and tree depth (top-down for deep trees to ~12 MiB,
bottom-up above); these forced numbers show why.

**8 MiB** (top-down uses the 2048-output chunk):

| dataset | bottom-up | top-down | top-down / bottom-up |
|---|---|---|---|
| calgary_pic | 34.2 | 44.9 | 1.31x |
| cat-image.jpg | 42.6 | 51.9 | 1.22x |
| cat-wiki.html | 27.5 | 37.2 | 1.35x |
| chinese_text.txt | 31.1 | 38.5 | 1.24x |
| csv_numeric.csv | 35.2 | 42.2 | 1.20x |
| dna_fasta.fa | 46.8 | 58.2 | 1.24x |
| gzip_random.gz | 119.2 | 117.4 | 0.98x |
| json_api.json | 27.4 | 36.5 | 1.33x |
| log_apache.log | 28.8 | 37.0 | 1.29x |
| pride.txt | 27.4 | 36.7 | 1.34x |
| source_c.c | 29.1 | 38.3 | 1.32x |

**64 MiB** (steady state; the cascade's O(depth) ALU hides behind DRAM latency):

| dataset | bottom-up | top-down | top-down / bottom-up |
|---|---|---|---|
| calgary_pic | 146.0 | 74.3 | 0.51x |
| cat-image.jpg | 122.1 | 74.6 | 0.61x |
| cat-wiki.html | 85.6 | 53.2 | 0.62x |
| chinese_text.txt | 92.5 | 55.4 | 0.60x |
| csv_numeric.csv | 113.4 | 67.3 | 0.59x |
| dna_fasta.fa | 147.1 | 99.7 | 0.68x |
| gzip_random.gz | 189.6 | 190.1 | 1.00x |
| json_api.json | 84.3 | 51.8 | 0.61x |
| log_apache.log | 86.7 | 52.8 | 0.61x |
| pride.txt | 85.8 | 54.3 | 0.63x |
| source_c.c | 78.0 | 54.4 | 0.70x |

At 8 MiB the top-down wins 1.20-1.35x on every compressible dataset (it pays 3
kernel launches vs the cascade's ~30, and its intermediates never leave shared);
`gzip_random.gz` (incompressible, shallow tree, fast baseline) is a 0.98x tie.
At 64 MiB the cascade wins ~1.5-2x: the per-chunk top-down exposes the
wavelet-tree O(depth) ALU serially, while the cascade hides that same ALU behind
DRAM latency at full occupancy (see DEVELOPMENT_LOG rounds 9-11). The crossover
is ~12 MiB for deep trees; `gzip_random.gz` ties everywhere (both ~190 GiB/s at
64 MiB -- its shallow tree makes the cascade cheap).
