# PivCo-Huffman GPU Decode — Optimization Idea Queue

Working queue for the bottom-up decode optimization effort. High-level context in
`DEVELOPMENT_LOG.md`; per-dataset numbers in `BENCHMARKS.md`. Status legend:
**QUEUED** / **IN PROGRESS** / **DONE** / **TRIED-NO-WIN** / **REJECTED**.

Profiling summary (json, the hardest deep-tree real file): decode ~51 GiB/s,
split VV-merge ~47%, rank directory ~20%, flat ~15%, constant/vector ~6%, parse
~6%. The VV merge is latency bound (Long Scoreboard 66% of cycles) on L2-resident
child gathers, NOT DRAM bound (43% DRAM). Deep trees pay O(N x depth) merge work
across a cascade of dependent per-level kernels. **The convergent conclusion from
a 5-thread literature search: the bottleneck is the on-device dependent
round-trip per tree level, not launch overhead or bandwidth. Highest-value moves
either collapse the O(depth) cascade so intermediates stay on-chip, or run many
independent dependency chains concurrently to hide the ~200-cycle L2 latency.**

## Active queue (priority order)

0. **REJECTED (measured -37%) — Radix-4 level fusion.** Implemented root-scoped
   (buffer-safe: root writes dst): decode the root + its 2 internal children in one
   kernel, gathering from the 4 grandchildren via 3 byteMerge8, skipping the
   level-1 child materialization. Correct, but json 65 vs 103 GiB/s. The fused
   child level operates at the data-dependent `childPos = node_rank0(j)` -- an
   UNALIGNED bit position -- so its rank falls onto the slow general
   `dRankSelectOnesBefore` (byte-by-byte `dLoadLe32Masked` + popcount) and its
   bitmap bits onto `dGetBits`' bit-by-bit loop, plus a 3rd/2nd directory build;
   this compute overwhelms the ~1/3 intermediate-traffic savings. The general
   version would be worse (also breaks the 2-buffer ping-pong: a node at level L
   reads L+2 which shares its buffer parity -> needs a 3rd buffer). Definitively
   killed. (Below: the earlier analysis that predicted this.)

   Earlier analysis (confirmed by the measurement): The child load is a
   concurrency floor (round-7 research: Little's Law -- warps maxed, MLP=2
   register-capped, latency fixed; `byte_perm` LUT expand already SotA;
   scatter/running-cursor/wide-load/prefetch/cp.async all neutral-or-worse). The
   structural lever would be fusing a node + its two INTERNAL children into one
   kernel that gathers from the 4 grandchildren (3 byteMerge8: gc0,gc1->child0;
   gc2,gc3->child1; child0,child1->parent), skipping child materialization.
   Detailed analysis says it is likely net-negative on this workload, for the same
   reasons the megakernel/cp.async lost:
   - **Unaligned child level.** The fused child cursor `childPos = node_rank0(j)`
     is data-dependent, so the child-level rank + bitmap-byte extraction fall off
     the byte-aligned fast path onto bit-level ops -- extra compute on an already
     58%-compute-SOL kernel.
   - **Register pressure -> occupancy.** The 2-level byte_perm needs 4 grandchild
     windows + 2 child windows live; ~48-64 regs drops from 8 to ~5 blocks/SM, and
     the size sweep proved the workload is parallelism-saturated (losing blocks
     hurts).
   - **Upside only ~1/3.** It reads 4 grandchildren vs 6 stream-reads for two
     normal levels (saves the 2 intermediate child reads + 2 writes), not a 2x load
     cut. Root-only radix-4 == the old `Root2` fusion (+3-6%, previously removed as
     not worth the complexity); the general version adds irregular-tree scheduling
     on top. Verdict: high complexity + occupancy risk for ~1/3 traffic savings;
     parked below the measured floor unless a future idea removes the unaligned
     child-level cost.

1. **QUEUED — Recover the `calgary_pic` -7% regression.** After the aligned-load
   win, calgary's VV merge is compute bound (68% SM, 6.8% DRAM): its small
   cache-resident child streams make the funnelshift cost more than the
   sector-sharing saves. Options: cheaper byte funnel via PRMT (`__byte_perm` on
   the two words instead of shift/or), or gate aligned vs unaligned load by a
   compressibility/stream-size proxy. Payoff: recover one real-file regression;
   risk: low.

1b. **TRIED-NO-WIN — Explicit MLP in the merge (batch K chains).** [Strategy 5]
   Restructured `byteMergeThread` into phases (all K ranks, then all K gathers,
   then all K merges). K=2 gave ~1.5% (noise); K=4 regressed (occupancy 89->68%
   from register pressure). The dependent rank->gather chain and register budget
   cap the benefit. Reverted.

2. **TRIED-NO-WIN — Warp-tile merge (coalesced load + `__shfl` distribute).**
   Each warp loads a 256-output tile's child streams coalesced & non-overlapping
   (2x vs the aligned load's 4x), warp-scans the rank, distributes 8-byte windows
   via `__shfl`+funnelshift. Correct but json 51 vs 68 -- the shfl/scan compute
   (4 shfl + 5-step scan per group) exceeds the halved memory traffic (same lesson
   as the 1-byte warp-expand). `byte_perm` density + simple aligned loads win; the
   merge inner loop is near-optimal. The 256-entry PRMT-LUT variant would pay the
   same cross-lane distribution cost -- deprioritized.

3. **REJECTED — Megakernel / hybrid level fusion in shared memory.** A
   working-set size sweep (json, 20 iters) settles it: decode throughput *rises*
   with more blocks -- 256blk 24, 512 46, 1024 61, 2048 78, 8192 84 GiB/s --
   and plateaus at ~2048 blocks. If the level-synchronous schedule were L2-thrash
   bound, a smaller (L2-resident) working set would be *faster* per byte; instead
   it is *slower* (fewer blocks = under-occupied). The decode is parallelism /
   latency bound and already parallelism-saturated at full size. Any block-sync
   megakernel (or top-M shared fusion) trades away that block parallelism for L2
   residency the workload does not need -- which is exactly why both prior
   single-CTA attempts lost. Do not pursue. The 62% L2 hit / 38% DRAM on the
   merge is not hurting because 8192-block parallelism hides the latency. Refocus:
   per-kernel compute/latency and total work, not memory-residency restructuring.

4. **QUEUED — Wide-lane rank/select (uint32/uint64 lane).** [Strategy 3] Replace
   the 8-bit lane with a 32- or 64-output lane: one `__popc`/`__popcll` for the
   lane's ones, a 5-step `__shfl_up_sync` warp scan for the cross-lane base, and
   **derive rank0 = i - rank1** (drops a whole scan). Optional two-level sampled
   popcount directory (superblock 512b + block 64b) per the GPU wavelet-tree
   thesis (arxiv 2505.03372, A100 rank >=10x CPU). Payoff: medium-high; risk:
   register pressure at uint64.

5. **QUEUED (tooling) — Kernel-level microbenchmarks (ideal-case per-kernel
   ceilings).** Build a standalone harness that runs each decode kernel (VV/CV/VC
   merge, flat<depth>, directory build, parse) in isolation on synthetic inputs
   sized to fit L2, repeated many times so inputs stay warm -- measuring each
   kernel's *ideal* throughput (no level-cascade L2 thrash, no cross-kernel
   interference). Compare against the in-pipeline nsys time to quantify how much
   each kernel loses to the level-synchronous schedule (the suspected merge L2
   thrash) vs its own inefficiency. This tells us, per kernel, whether it is at
   its floor or has headroom, and directly sizes the megakernel/L2-residency
   payoff before we build it. Payoff: high (de-risks and targets all further
   per-kernel work); risk: low (pure measurement tooling). Do this early.

6. **QUEUED — Directory kernel: pack small nodes, cut wasted CTAs.** Grid is
   ~90k CTAs of 256 threads; most internal nodes are tiny and use ~1 warp.
   Process several small nodes per CTA or use a smaller blockDim for the small
   stage. (Now folded into the merge via dir-fusion on the bottom-up path; still
   relevant for the top-down path's standalone directory kernel.) Payoff: low-med.

6. **QUEUED — Rank-directory software prefetch.** [Strategy 6] Directory
   addresses are index-derived and known ahead, so prefetch `rank[i+PDIST]` while
   working on `i` (the child gather is NOT prefetchable -- depends on the rank).
   Route child stream through `__ldg`/read-only path to spare the L1 the rank
   loads use. Payoff: partial (first hop only); risk: low. Composes with idea 1.

7. **QUEUED — cp.async double-buffer child gathers to shared.** [Strategy 7]
   `cuda::memcpy_async`/`__pipeline_*`, 16-byte `.cg` copies, S=2/3 stages, to
   overlap the L2 latency non-blockingly and off the register path. Only pays off
   with real pipelining; largely subsumed by idea 3 if that lands. Payoff:
   conditional; risk: medium.

8. **QUEUED — L2 residency / eviction hints.** [Strategy 8] Cheap: tag ping-pong
   reads `__ldcg`, raw input / final output `__ldcs`/`__stcs` (streaming), and
   `discard` fully-consumed child streams. Bigger hammer: persist the small hot
   metadata (rank directories) via `cudaAccessPolicyWindow` hitRatio=1.0 (footgun:
   thrash if window > set-aside). Payoff: medium; risk: low (hints) / medium
   (persist).

9. **QUEUED — Parallelize `scheduledParseKernel` (1 thread/block today).** ~6%.
   Cooperative parse across the CTA. Payoff: small; risk: low.

10. **DONE (big win) — Block-size reselected to 64 KiB.** Sweep showed 16 KiB
    slower (2x per-block overhead) and >32 KiB fell back to the generic decoder.
    Raised `kRankSelectMaxBlockSize` to 64 KiB (merge shared dir ~8 KiB/CTA,
    occupancy unchanged at 8 blocks/SM) and set the default to 64 KiB. LARGER
    blocks (not smaller) win: the tree/node count is fixed by the symbol
    distribution, so 64 KiB halves the block count and thus the per-block overhead
    (parse, per-node directory builds, launches) at no merge cost. ~+15%
    everywhere, slightly better ratio, no real-file regression. Committed. (Note:
    CUDA Graphs would only remove host launch bubbles -- not the on-device
    bottleneck -- so not pursued.)

## Update (2026-07-20): DONE (big win) — vectorized the flat-root fast path

The prior effort concentrated entirely on the scheduled-cascade slow tier and
treated the "fast paths" as done. But `fastDecodeFlatRootKernel` was never given
the multi-symbol treatment its scheduled sibling (`scheduledFlatKernel`) got:
it decoded one symbol per thread (dependent 1-2 byte load + bank-conflicted
256-entry shared gather + byte store). `ncu` on `uniform`: DRAM 23% / compute
21% / mem 24% SOL, 4.5M shared bank conflicts -- ~4x below roofline, latency
bound. Rewrote it as 8 outputs/thread + one packed load + register unpack + one
coalesced 8-byte store + depth-2 MLP. Result: sparse/flat/uniform/gzip
(flat-root data) +149..+255%; overall geomean 189->249 (+31.5%), arithmetic mean
233->358 (+54%), no regressions, byte-identical, all tests pass. Lesson: audit the
"already fast" fast paths against their own roofline -- 250 GiB/s looked fast next
to the 100 GiB/s slow tier but was 4x under its own ceiling.

Also re-confirmed (independent `ncu`, contradicting the earlier "at DRAM roofline"
framing but agreeing with the concurrency-floor conclusion): the VV merge is
*latency* bound, not throughput bound -- across its per-level launches compute
peaks ~71%, DRAM peaks ~64%, neither saturates, CPI ~19-30 at ~78% occupancy with
warps maxed. So the slow tier's remaining gap is dependent-load latency at a
genuine concurrency floor, unmovable without an algorithm/format change.

## Status (2026-07-18): queue largely exhausted

Seven wins landed (flat unpack, aligned merge loads, directory launch_bounds,
directory-into-merge fusion, byte-wise readBits, CC vectorization, 64 KiB blocks)
-> ~2x on the slow tier this session. The vector/vector merge (now ~63% of the
deep-tree cost) profiles balanced (58% compute, 60% memory, 91% occupancy) and is
at its per-kernel floor; the workload is parallelism-saturated (megakernel
REJECTED). Remaining queue items 4/6/7/8 all target the merge's memory/latency,
which is no longer the sole limiter (it's balanced), so their expected payoff is
now small; item 9 (parse) dropped to ~4% after 64 KiB. The kernel microbench
(item 5) is built and confirms the floors.

### Conclusion (2026-07-18): bottom-up decode is at its architectural floor

A second research pass and trying its top idea (cp.async streaming) confirms it:
the vector/vector merge is at its A100 floor -- balanced compute+memory, max
occupancy (64 warps/SM), latency-bound on the monotone child stream, and every
latency-hiding restructure (megakernel, warp-tile shuffle, cp.async shared
streaming) loses to the plain `__ldg` + read-only-cache merge because the workload
is parallelism-saturated and those schemes trade away occupancy / add overhead.
MLP is register-capped; directory coarsening adds net compute; `__ldcg`/unaligned
loads lose the sector dedup. Flat is output-bandwidth-bound; parse is at its
serial floor; CC/directory are vectorized/fused. **Another multiple would require
a different algorithm or hardware** (top-down ~2x slower; per-symbol walk
diverges; Hopper TMA/DPX absent on A100 and don't map to byte routing). Session:
~2x on the slow tier, multiple-x over the original baseline, no regressions.

## Tried

- **TRIED-NO-WIN — Break the child-load latency (round 7, research-driven).** The
  merge is latency-bound on the dependent child load (rank->load->byte_perm).
  Little's-Law framing (research agent): at the 64-warp/SM ceiling with MLP~2 and
  fixed L2 latency, `bytes_in_flight` is pinned, so only MLP or latency are free.
  Tried: (a) `prefetch.global.L2` a fixed distance (512 B) ahead of the monotone
  child cursor -- neutral (ptxas often elides it; the monotone window is already
  L2-warm from read-only-cache sector reuse); (b) explicit depth-2 software
  pipeline (issue next group's rank+loads before consuming current) -- neutral,
  because `unroll=2` already gives MLP=2 and deeper is register-capped (the K=4
  cliff). Warp-specialization and L2 persisting-window were assessed as
  net-negative/bounded for this parallelism-saturated profile. Confirms the merge
  is at a genuine concurrency floor; a structural (algorithmic) change is required.
- **TRIED-NO-WIN — cp.async double-buffered shared streaming of child slices
  (research Idea 1).** The strongest novel idea: since rank is monotone, each
  warp's contiguous output tile consumes two contiguous child slices, so
  double-buffer them into a per-warp shared ring with `cp.async` (hiding the
  L2/DRAM child-load latency off the critical path) and read child bytes from
  shared. Implemented and correct, but json 75 vs 101 GiB/s -- the shared-staging
  + funnel-shift-from-shared + `__syncwarp`/pipeline overhead exceeds the latency
  hiding. Same root cause as the megakernel: the workload is parallelism-saturated
  at max occupancy, so the plain `__ldg` merge (HW unaligned load + read-only
  cache sector dedup) already hides the latency with its 64 warps/SM, and any
  shared-buffer scheme just adds overhead. This also moots research Ideas 2 (only
  helps paired with Idea 1) and 3 (streaming fusion built on Idea 1). Reverted.
- **TRIED-NO-WIN — `__ldcg` (cache-global, bypass read-only) child loads.** Net
  -2-3% vs `__ldg`; the read-only cache's cross-thread sector dedup on the
  overlapping monotone child windows is worth more than L1 bypass. Reverted.
- **DONE (tooling) — `PIVCO_KERNEL_TIMING` per-kernel microbench.** Env-gated
  per-stage CUDA-event timing in the bottom-up dispatch; prints the isolated
  per-kernel GPU time (parse + each merge/flat op). Off by default (perf path
  unchanged). Confirmed the vector/vector merge is ~60% (json mergeVV=0.93ms) and
  the constant/vector merge dominates skewed real files (calgary CV>VV).
- **TRIED-NO-WIN — Unaligned load for the constant/vector merge.** CV/VC profile
  as compute bound (58% SM, memory headroom), so I tried the plain unaligned load
  (no funnel-shift) for the single vector child, keeping aligned for VV. Regressed
  (calgary 192->178): even "compute-bound" CV is really latency-bound (43%
  no-eligible), and the aligned read-only load's sector sharing beats the saved
  funnel-shift compute. The aligned load is universally best. Reverted.
- **DONE — Aligned read-only child loads in the merge (`dLoad8Aligned`).** Two
  aligned `__ldg` 8-byte loads + funnelshift instead of one unaligned load;
  overlapping thread windows share sectors via the read-only cache. VV merge -26%.
  Decode +30-68% on merge-heavy datasets. Committed. (Realizes the coalescing goal
  of queue idea 2 more simply than the PRMT-LUT expand, which remains available to
  stack on top.)
- **DONE — Multi-symbol per-thread flat unpack.** 8 outputs/thread, one load of
  the group's `Depth` bytes, unpack 8 indices, coalesced 8-byte store. flat1 on
  dna 2.7x (943->350 us). Decode: dna 102->120, json 51->58, csv 71->77. Committed.
- **DONE — Directory `__launch_bounds__(256, 8)`.** 40->32 regs, 63->85%
  occupancy. Directory kernel 522->449 us (~14%). Committed.
- **REJECTED — Warp expand merge at 1 byte/lane.** json 27.8 vs ~51 (2x slower);
  no single-instruction GPU warp expand, 2 shfl+ballot+popc per output with no
  ILP. Matches extras/gpu Metal notes ("1 byte per lane too thin").
- **TRIED-NO-WIN — Merge `#pragma unroll` 4 and/or `__launch_bounds__`.** VV merge
  time unchanged (already 8 blocks/SM; compiler didn't reorder to batch loads --
  hence explicit-MLP idea 1).
- **TRIED-NO-WIN (earlier) — Plain shared-staging of child streams.** Regressed
  ~15%: moves efficient HW-unaligned 8-byte global loads to byte-wise shared
  reads; intermediates already L2-resident.

## Parked / structural (revisit if queue exhausted)

- **Full single-CTA-per-block tree walk** (all levels in shared). Lost before;
  idea 3 is the salvageable hybrid.
- **Per-symbol top-down rank/select walk** (no intermediate streams). Killer is
  warp divergence + scattered bitmap reads.
- **DONE (big win, not break-even) — Fuse directory build into the merge.** Each
  merge kernel now builds its node's rank directory in shared memory
  (`buildSharedDirectory`) and the standalone directory kernel is dropped on the
  bottom-up path. The earlier "break-even" estimate missed that the standalone
  kernel forced a redundant bitmap DRAM re-read (evicted from L2 between the two
  launches) plus the directory global write+read. +40-92% across merge-heavy
  datasets; also recovered the calgary aligned-load regression. Committed.
- Wire-format tricks that do NOT apply (fixed bitstream): GDeflate/Brotli-G 32-way
  substream swizzle, DietGPU interleaved rANS, Yamamoto gap arrays, Weissenberger
  self-sync. Relevant only if a format revision is ever on the table.

## Investigated & rejected: cp.async shared-staging of merge inputs (round 8)

Staging the vec/vec & cst/vec child inputs into shared (cp.async double-buffered,
coalesced) was built end-to-end and REJECTED as a net ~10% regression (see
DEVELOPMENT_LOG round 8 for the A/B and profiling). Keep as the best-known staged
design if per-output merge ALU is ever reduced:
- Rank is monotonic => a contiguous output chunk maps to a contiguous slice of each
  child (numL+numR == chunk outputs). Chunk = 2048 outputs.
- cp.async (`__pipeline_memcpy_async`, 8B, from the 8-aligned floor + head offset)
  double-buffers each child slice global->shared, overlapped with the prior chunk's
  merge (`__pipeline_commit` / `__pipeline_wait_prior(1)`).
- Merge with `byteMerge8` from shared (`dLoad8Shared`), folding the chunk's child
  base into the shared pointer so per-output cursor math == the global path.
- Consolidate the 2 parity buffers into one array/child (base+parity*stride) to keep
  registers at 32 => 8 blocks/SM (92% occupancy). `__launch_bounds__` spills.
- Blocker: becomes ALU-bound (~1.5x baseline ALU); the coalescing doesn't help
  because the baseline is already DRAM-min (aligned read-only gather dedups sectors).

Follow-on ideas that WOULD make staging (or the baseline) win -- all target
per-output merge ALU, the true ceiling:
- Cheaper rank: avoid `dRankSelectOnesBeforeAligned` per 8-output group; derive
  intra-group rank from the bitmap word via `__popc` (warp-cooperative) so only one
  directory read per 32 outputs.
- Lighter window extraction: the `dLoad8*` funnelshift is 2 loads + shifts/child;
  find a merge that needs fewer per-output integer ops.
- Per-node dispatch: staging wins on csv (+4%); a shape/size heuristic could pick
  staged vs baseline per node.

## Chunk-TD (round 9) -- shipped for small inputs; next levers to raise the crossover

Status: the chunk-in-shared top-down decoder is auto-dispatched for dstSize <= 8 MiB
(1.3-2.3x faster there via 3 launches vs the cascade's ~30). Large inputs use the
cascade (chunk-TD is wavelet-tree ALU-bound once DRAM is removed; ~2.5x slower).
Raising the chunk-TD's throughput raises the crossover, expanding the win regime.

Remaining decode-only levers (not yet done), ranked:
1. **Radix-4 (2-level) fusion in shared** -- compute a node's output directly from
   its 4 grandchildren, skipping the child level's materialization. Now viable
   (shared kills the unaligned-rank penalty that made it -37% in L2). ~1.4-1.8x on
   the merge, ~2x crossover. Complex: reconstruct both child windows in registers
   via 2 extra byteMerge8 + 2 unaligned shared ranks per parent group; only when
   both children are internal (fall back at leaf boundaries); partial-fusion per
   node makes the ping-pong bookkeeping tricky. Highest value, highest risk.
2. **Cursor-carry variant for small inputs** -- warp-per-block processing its
   chunks sequentially carries per-node rank cursors, eliminating the directory
   kernel (3 launches -> 2) and its build/read. Occupancy-irrelevant at small
   scale (the only regime chunk-TD runs). Cuts fixed overhead where it matters.
3. **Parse parallelism** -- scheduledParseKernel is <<<numBlocks,1>>> (serial tree
   parse per block); at small sizes with few blocks it underutilizes. Inherently
   serial per block (sequential bitstream), but helps both paths if parallelized.
4. **Per-tree-depth crossover threshold** -- shallow trees (dna) win to ~12+ MiB,
   deep (json) to ~10 MiB; a depth-aware threshold beats the fixed 8 MiB.

Rejected (measured/analysed): table-driven multi-symbol decode (transposed bitmap
layout has no codeword stream; assembling one IS the O(depth) walk); s_nodes shared
cache (marginal -- kernel is ALU- not memory-bound).

Aligned uint64 stores on many-node levels: **MEASURED -24% (Round 10), rejected.**
Padding levels with 8..31 nodes (to 8-byte-align their stores, replacing ~8 STS.U8
with 1 STS.U64) grew the per-warp buffer ~224 B and crossed a 6->5 block occupancy
cliff; same-size @64MB A/B lost ~24% uniformly. **Occupancy-cliff insight:** the
kernel's ~22% stall cycles are hidden by occupancy, so it is NOT purely issue-bound
-- any instruction cut that costs shared memory backfires. This raises the risk on
lever #1 (radix-4): even though it is shared-neutral, its 2 extra byteMerge8 +
child-window regs per group add register pressure that could cross the register
occupancy cliff (already at 40 regs / 6 blocks); combined with the -37% L2 result,
its realistic ceiling is parity with the baseline, not a clear win.

Latent (pre-existing): forced chunk-TD trips a 1-byte __global__ over-read on the
last block under compute-sanitizer (rank/bitmap helpers read a few bytes past the
bitstream by design, "into trailing slop, all masked off"; the last block has no
next-block slop). Harmless in deployment (allocator rounding + masked off); a future
robustness pass should give `src` a few bytes of trailing slop.

## Round 11 outcome: chunk width was the lever (LANDED, b2bb5b0278cb)

The occupancy-cliff insight (Round 10) pointed here: the per-chunk tree descent is
fixed overhead, so a wider chunk amortizes it. Chunk width is now runtime-selected
by input size (1024 tiny / 2048 >6 MiB) and the auto crossover is shape-gated on
tree depth (deep tableLog>=10: 12 MiB; shallow: 4 MiB). Result: +41..79% @4 MiB and
+15..38% @10 MiB across all real datasets, no regression; the 8-12 MiB regime moved
from cascade to chunk-TD. Note this raised the chunk decoder's CEILING (extending
its regime), not the large-input floor -- >12 MiB is still the cascade (Round 10).

Next levers if pushing further: (1) sweep the 1024/2048 boundary and the 12 MiB
cap per tree shape (currently a coarse tableLog gate -- a smarter depth/skew model
could reclaim the 16 MiB dna win without the calgary regression); (2) the descent
(Phase 0/1) is now a larger fraction at 2048 -- parallelizing scheduledParseKernel
(still <<<numBlocks,1>>>) or fusing the directory build would cut the fixed cost
further and could push the crossover past 12 MiB. Radix-4 remains parity-ceilinged
(-37% measured) and is not worth it.
