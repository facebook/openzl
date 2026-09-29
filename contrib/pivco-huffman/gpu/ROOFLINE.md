# PivCo-Huffman GPU Decode — Roofline Analysis

Device: **NVIDIA A100 (SM 8.0)**. Measured with `ncu` (SpeedOfLight +
MemoryWorkloadAnalysis), 64 KiB blocks, on the committed decoder. Goal: for each
core kernel, place it on the roofline and flag the ones **below** their roofline
(the optimization targets).

## Method

For these integer/byte-shuffle kernels there are no FLOPs, so the roofline is
expressed in **output bytes/s vs arithmetic intensity AI = output bytes moved per
DRAM byte moved**:

```
achievable_output_Bps(AI) = min( compute_roof , peak_DRAM_BW x AI )
```

A kernel is **at its roofline** when it saturates one ceiling, i.e. when
`max(Compute-SOL, Memory-SOL) ~ 100%` (SOL = Speed-Of-Light = % of the hardware
peak that ncu reports). The key identity that makes ncu's SOL numbers *be* the
roofline position:

```
achieved_output_Bps / memory_roof
   = (W/t) / (peak_BW x W/Q)          [W = output bytes, Q = DRAM bytes, t = time]
   = Q/(t x peak_BW)
   = achieved_DRAM_BW / peak_DRAM_BW
   = DRAM-SOL %
```

So **DRAM-SOL % is the fraction of the memory roof achieved**, and **Compute-SOL %
is the fraction of the compute roof**. `max` of the two = distance to the roofline;
anything well under 100% is **latency-bound (below the roofline)**.

### Measured peak

`ncu` on the VV merge reports DRAM `1.12 TB/s at 57.69%` -> **peak DRAM BW =
1.12/0.5769 = 1.94 TB/s** (this is an 80 GB A100). L2 BW is several x higher, so
L2-resident traffic is not the binding roof for these kernels.

## Arithmetic intensity (the math)

`AI = 1 / (DRAM bytes moved per output byte)`. Minimum DRAM bytes/output `B_min`
(the rank directory is built in shared from the bitmap, so it adds no DRAM beyond
the one bitmap read):

- **VV merge** `out[i] = bit ? right[rank1] : left[rank0]`: read both children
  (`rank0`+`rank1` cover every output exactly once => **1 B/out**) + read bitmap
  (**1/8 B/out**) + write parent (**1 B/out**) = **2.125 B/out**, AI = **0.471**.
- **CV merge** `out[i] = bit ? right[rank1] : leftSym` (left constant): read only
  the vector child (`numOnes` bytes => `p1 = numOnes/count` B/out, ~0.5 typical) +
  bitmap (1/8) + write (1) = **~1.6 B/out**, AI = **~0.63**.
- **flat<D>** `out[i] = symbols[ D-bit index i ]`: read the packed indices
  (`D/8` B/out) + write (1) + the 2^D-byte table once (negligible) = **(1 + D/8)
  B/out**, AI = **8/(8+D)**:

| kernel | B_min (B/out) | AI (out-B/DRAM-B) | memory roof (output GB/s = 1.94 TB/s x AI) |
|---|---|---|---|
| VV merge | 2.125 | 0.471 | 913 |
| CV merge | ~1.6 | ~0.63 | ~1210 |
| flat1 | 1.125 | 0.889 | 1724 |
| flat2 | 1.250 | 0.800 | 1552 |
| flat3 | 1.375 | 0.727 | 1411 |
| flat4 | 1.500 | 0.667 | 1293 |
| flat5 | 1.625 | 0.615 | 1194 |
| flat6 | 1.750 | 0.571 | 1109 |
| flat7 | 1.875 | 0.533 | 1035 |
| flat8 | 2.000 | 0.500 | 970 |

Sanity check on the VV over-read: the aligned `__ldg` load fetches 16 B per 8-out
window per child (nominal 4x over-read), but ncu shows the VV launch moves
`1.12 TB/s x 258 us = 289 MB` DRAM for a 134 MB-output root launch = **2.15 B/out
~= the 2.125 B/out minimum**. The 62% L2 hit absorbs the over-read; VV is **not**
wasting DRAM bandwidth, so its AI is effectively the minimum.

## Roofline position (measured, largest launch of each kernel)

Measured at the 268 MB benchmark scale (largest launch of each cascade kernel).
The per-kernel SOL below is a hardware-relative measure and these kernels are
unchanged; a spot re-measure at 64 MiB (below, next to the top-down section)
reproduces the same latency-bound story at slightly lower absolute SOL (fewer
waves in the smaller launches).

| kernel | Compute-SOL | Memory-SOL | DRAM-SOL | L2 hit | bound by | % of its roofline | at roofline? |
|---|---|---|---|---|---|---|---|
| **VV merge** | 48.6% | 57.7% | 57.7% | 62% | memory (DRAM), latency-limited | ~58% | **no** |
| **CV merge** | 59.7% | 41.8% | 23.2% | 88% | compute, latency-limited | ~60% | **no** |
| **flat1** (big, 145us) | 57.3% (ALU) | 54.5% | 54.5% | (L2 59%) | balanced, latency-limited | ~57% | **no** |
| flat2 (big) | 43.1% | 33.2% | 30.2% | - | ALU-leaning, latency | ~43% | **no** |
| flat3 (big) | 48.6% | 43.4% | 43.4% | - | balanced, latency | ~49% | **no** |
| flat4 (big) | 45.8% | 52.3% | 52.3% | - | memory | ~52% | **no** |
| flat5 (big) | 46.3% | **62.9%** | 53.9% | - | memory | ~63% | **no** (closest) |
| flat6 (big) | 43.8% | 58.8% | 49.4% | - | memory | ~59% | **no** |
| flat7 (big) | 46.0% | **63.5%** | 60.5% | - | memory | ~64% | **no** (closest) |
| flat8 | (not exercised; extrapolates to flat7: memory, ~65%) | | | | | | |

The flat1 numbers were re-measured directly on `dna` (the flat1-heaviest dataset):
the one large flat1 launch (145 us, grid 4096) runs at **ALU 57.3% / DRAM 54.5% /
L2 59.3%, IPC 2.17 of 4, issue-slots 53.6%** — i.e. **balanced and latency-limited**,
with ALU the leading pipe but no pipe near its roof. (An earlier draft of this doc
mis-recorded flat1 as "52% compute / 1.2% DRAM"; that was a transcription error and
is corrected here.)

Small flat launches (the common case — flat leaves are small subtrees) sit far
lower: ~43% ALU / ~22% memory (e.g. the dozens of ~14 us dna flat1/2/3 launches).
They are **latency-bound on small per-node work**, ALU-leaning but with the ALU pipe
itself less than half busy.

## Findings — nothing is at its roofline; where to look

**Every core kernel is below its roofline** (all `max(Compute,Memory)-SOL` in the
40-64% range). The reasons split into three groups:

1. **Merges (VV, CV) — latency-bound at a genuine concurrency floor.** VV is at
   ~58% of the memory roof (DRAM-bound at the minimum AI) and CV at ~60% of the
   compute roof; both stall ~55% of cycles on the dependent rank->child-load chain
   even at 64 warps/SM (max occupancy) with MLP=2 (register-capped). This is the
   `bytes_in_flight = warps x MLP x bytes/load` ceiling (Little's Law). The one
   structural lever to raise it (radix-4 level fusion, cutting dependent-load
   levels) was implemented and **measured -37%** (unaligned fused-child ops
   dominate; see DEVELOPMENT_LOG). **Verdict: at the practical floor; no cheap win.**

2. **flat kernels — also latency-bound; ALU-leaning at low depth, memory-leaning
   at high depth.** The single large flat1 launch (145 us on dna) runs balanced at
   **ALU 57.3% / DRAM 54.5%, IPC 2.17 of 4, issue-slots 53.6%** — the ALU is the
   leading pipe (the 8x shift+mask+lookup unpack) but no pipe is near its roof, so
   it is latency-limited, not throughput-bound. High-depth flats (flat4-7, big
   launches) shift to memory-leaning (52-64% DRAM) because they read more packed
   bytes; flat5/flat7 at ~63% are the closest anything gets to a roof. flat1 is
   **not** the compute-bound non-floor gap an earlier draft claimed.
   - Still worth trying: cut the low-depth unpack instruction count (register
     2-entry select for D=1 like the CC path; PRMT multi-symbol unpack for D=2-4).
     Because these kernels are latency- not ALU-throughput-bound, expect a small
     win at best. A prior register-LUT swap measured neutral overall.

3. **Small flat launches (all depths) — latency-bound on tiny leaf subtrees**
   (~43% ALU / ~22% memory, dozens of ~14 us launches). This is the common case and
   the largest aggregate flat cost. Inherent to the tree shape; depth-2 MLP already
   added. Not a roofline gap code can close (the work per node is just small); the
   only lever is fewer/larger launches (batching sibling flat nodes).

### Ranked optimization targets (from this analysis)

Honestly, **all core kernels are latency-bound below their roofline**, and the two
biggest (VV, CV) are at a genuine concurrency floor. There is no kernel sitting at a
throughput roof with a clear structural win. Ordered by remaining (modest) ROI:

1. **Small flat launches** — the largest aggregate cost that is *not* a concurrency
   floor. The lever is launch/occupancy structure (batch sibling flat nodes into
   fewer, larger launches), not the inner loop.
2. **Low-depth flat unpack (flat1-3)** — trim unpack instructions (register select /
   PRMT); latency-bound, so likely small.
3. **VV/CV merges** — largest kernels but at the concurrency floor; only a
   dependency-removing algorithm change would move them, and the one identified
   (radix-4) measured -37%. Low ROI.
4. **flat4-7** — near the memory roof; low ROI.

## Top-down (chunk-in-shared) decoder at 64 MiB

Roofline for `pivcoChunkTopDownKernel` (the merge-in-shared decoder), forced
(`PIVCO_CHUNK_TD=1`) at 64 MiB, DCGM muted, one launch per dataset via
`ncu --kernel-name regex:pivcoChunkTopDownKernel --launch-count 1`. At 64 MiB the
chunk width is 2048 (dstSize > 6 MiB). Measured on the committed decoder:

| dataset | Compute-SOL | Mem-SOL | DRAM-SOL | L2 hit | L1 hit | IPC (of 4) | issue-slots | occupancy (ach/theo) | decode GiB/s | bound by |
|---|---|---|---|---|---|---|---|---|---|---|
| json_api.json | 60.1% | 42.8% | 5.3% | 83.6% | 84.7% | 2.43 | 59.9% | 47.7% / 50% | 46 | compute, latency/occupancy |
| dna_fasta.fa | 55.9% | 41.1% | 6.5% | 91.4% | 80.2% | 2.31 | 55.9% | 46.7% / 50% | 100 | compute, latency/occupancy |
| calgary_pic | 53.5% | 38.4% | 4.7% | 92.3% | 84.4% | 2.22 | 53.5% | 43.5% / 50% | 74 | compute, latency/occupancy |

Reg/thread = 40; dynamic shared = 36.1 KiB/block. ALU is the top pipe (41-49% of
elapsed). The 36.1 KiB/block is `levelNodeBytes + 8 warps x perWarpBytes`, where
`perWarpBytes = 2 x (chunkOutputs + 64) + 6 x nodeCount`. For json (~61 tree
nodes; nodeCount << symbol count because FLAT leaves each decode 2^depth symbols):

| component | size | share |
|---|---|---|
| ping-pong stream buffers: 8 warps x 2 x (2048+64) B | 33.8 KiB | 91% |
| per-node metadata: 8 warps x 6 B (loBit/subLen/streamOff u16) x 61 nodes | 2.9 KiB | 8% |
| CTA-wide level-node list: 2 B x 61 | 0.1 KiB | <1% |

Each warp decodes one 2048-byte chunk by walking the tree bottom-up in shared,
reading child streams from one buffer and writing parents into the other
(ping-pong), so two chunk-sized buffers must be live -- and each holds a full
chunk (the nodes at any level partition the chunk). The wide 2048 chunk is the
whole reason it is 36 KiB: the 1024 chunk needs `8 x 2 x 1088 ~= 17.4 KiB` + meta
~= 19 KiB. That doubling is exactly what drops the block limit from 6 (1024,
register-bound) to 4 (2048, shared-bound) -> 72% -> 50% occupancy.

### Arithmetic intensity and roofline position

The kernel is designed to move the DRAM minimum: read the compressed slice once,
write the 64 MiB output once, and keep every intermediate node stream in shared.
`B_min` (DRAM bytes per output byte) = write (1) + read compressed (`ratio`):

| dataset | ratio | B_min | AI = 1/B_min | memory roof (out GB/s) | achieved | % of memory roof | % of compute roof |
|---|---|---|---|---|---|---|---|
| json | 0.65 | 1.65 | 0.61 | 1180 | ~49 | ~4% | ~60% |
| dna | 0.28 | 1.28 | 0.78 | 1516 | ~107 | ~7% | ~56% |
| calgary | 0.21 | 1.21 | 0.83 | 1604 | ~80 | ~5% | ~53% |

(memory roof = 1.94 TB/s x AI; achieved output Bps ~= decode GiB/s.) The measured
DRAM-SOL (5-6%) matches this `B_min` estimate, confirming the design goal: the
chunk decoder is **DRAM-idle** (5% of the HBM roof, no intermediate round-trips)
and therefore **far to the right of the ridge -- deep in the compute-bound
region**. But it is not at the compute roof either (Compute-SOL 53-60%), so like
every kernel in this codec it sits **below its roofline, latency-bound**.

### The binding constraint: shared-memory-limited occupancy

Unlike the 1024-chunk kernel (rounds 9-10: 40 regs -> 6 blocks -> 72% occupancy,
IPC 3.11), the 2048-chunk kernel's 36 KiB/block shared caps it at **4 blocks/SM ->
50% theoretical occupancy** (Block Limit Shared Mem = 4 < Block Limit Registers =
6). At 50% occupancy (~30 warps/SM) there are not enough warps to hide the
dependent rank -> shared-load -> byte-merge chain, so IPC falls to 2.2-2.4 of 4
(issue-slots 53-60%) and no pipe reaches its roof. This is the Little's-Law
concurrency floor again, but reached at lower occupancy than the 1024 kernel.

### Why top-down loses to the cascade at 64 MiB (roofline read)

The cascade (bottom-up) runs the SAME total wavelet-tree ALU but is DRAM-latency
bound at max occupancy (64 warps/SM), so that ALU executes for free in the shadow
of memory stalls -- it decodes json/dna/calgary at 84/147/146 GiB/s. Its dominant
kernel measured at 64 MiB (same conditions), the root VV merge on json, runs at
**Compute-SOL 45.7% / DRAM-SOL 52.1% / L2 hit 61.8%, 76 us** -- DRAM-leaning and
latency-bound, matching the 268 MB per-kernel table above (48.6% / 57.7% / 62%) at
slightly lower absolute SOL. The chunk
decoder deleted the DRAM traffic (63% -> ~5%) and thereby deleted the very thing
that was hiding the ALU; the wide 2048 chunk that wins at medium sizes (it
amortizes the fixed per-chunk descent) costs occupancy here (72% -> 50%), so at
64 MiB the exposed O(depth) ALU runs at only ~55% compute-SOL / 50% occupancy and
the kernel is ~1.5-2x slower. There is no roof to push against: raising throughput
needs either fewer instructions (radix-4 = -37%, round 10) or more occupancy
(blocked by the 36 KiB shared the wide chunk needs) -- the two trade off, which is
exactly why the shipped decoder switches to the cascade above ~12 MiB.
