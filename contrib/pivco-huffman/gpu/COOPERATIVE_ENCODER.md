# Cooperative PivCo GPU encoder

**Hardware conditions for all measurements:** Non-stock hardware: CPU power-limited for thermals; RTX 3080 Ti power budget increased with a more conservative clock curve; RTX 3060 connected through a PCIe x1 link. Exact tuning values were not recorded. These workstation measurements are not stock-hardware expectations.

Device-resident synthetic inputs: 4 MiB, 64 KiB blocks. Values are the mean
of two seven-iteration medians, in baseline/candidate/candidate/baseline order.
CUDA events include status resets, context upload and kernels; bulk transfers,
allocation and verification are excluded. Full bytes and offsets match CPU
encoding. The unchanged three-symbol path serves as a control.
Its timing differences are noise.

| GPU | Fixture | Baseline ms | Patched ms | Speedup |
| --- | --- | ---: | ---: | ---: |
| RTX 3060 | english | 42.2810 | 0.9134 | 46.29x |
| RTX 3060 | geometric | 32.9124 | 0.7609 | 43.26x |
| RTX 3060 | three_sym_50_25_25 | 0.1387 | 0.1413 | 0.98x |
| RTX 3060 | two_sym_eq | 19.7939 | 0.4541 | 43.58x |
| RTX 3060 | uniform | 30.4292 | 0.5048 | 60.28x |
| RTX 3060 | zipfian | 58.3987 | 1.2805 | 45.61x |
| RTX 3080 Ti | english | 39.8586 | 0.7109 | 56.06x |
| RTX 3080 Ti | geometric | 29.9857 | 0.5934 | 50.54x |
| RTX 3080 Ti | three_sym_50_25_25 | 0.0778 | 0.0809 | 0.96x |
| RTX 3080 Ti | two_sym_eq | 18.9496 | 0.3961 | 47.84x |
| RTX 3080 Ti | uniform | 29.2833 | 0.3861 | 75.85x |
| RTX 3080 Ti | zipfian | 53.7522 | 0.9713 | 55.34x |

Encoder scratch for 4 MiB grows from 8,390,160 to 16,790,272 bytes; this is a
workspace query, not total VRAM. Legacy-sized scratch selects the fallback.
The query also reserves extra scratch for the unchanged specialized path.
See [cooperative-benchmark.json](cooperative-benchmark.json) for raw medians,
source identities and coverage.

## Validation

All 19 GPU tests pass on both cards. The isolated decoder fix passes 12 tests;
the old decoder fails its CPU-wire regression. RTX 3080 Ti memcheck passes;
racecheck and synccheck pass with the largest-grid test excluded.

Tested: WSL2 Linux, CUDA 13.2, GCC 13.3, SM 8.6. Untested: HIP, older GPUs,
Windows-native builds and the full OpenZL suite. Both builds retain the existing
SM 8.6 scheduledDirectoryKernel launch-bounds warning. Constant decode remains
unsupported; the new constant-encode validation cost was not timed.

## Reproduce

```sh
mkdir -p build/empty-datasets
git show 55410503c2190d5e67469875d26e2972d969c586:contrib/pivco-huffman/gpu/pivco_gpu.cu > build/pivco_gpu_baseline.cu
cmake -S contrib/pivco-huffman/gpu -B build/pivco -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=86 -DPIVCO_GPU_BASELINE_SOURCE="$PWD/build/pivco_gpu_baseline.cu"
cmake --build build/pivco --parallel 2
ctest --test-dir build/pivco --output-on-failure
PIVCO_CHUNK_TD=1 build/pivco/pivco_gpu_regression_baseline --gtest_filter='*FullBlockAllOnes*'
# Expected assertion failure above; both patched paths below must pass.
PIVCO_CHUNK_TD=1 build/pivco/pivco_gpu_test --gtest_filter='*FullBlockAllOnes*'
PIVCO_CHUNK_TD=0 build/pivco/pivco_gpu_test --gtest_filter='*FullBlockAllOnes*'
```

Select a GPU with CUDA_VISIBLE_DEVICES. Repeat these in baseline/candidate/
candidate/baseline order:

```sh
build/pivco/pivco_gpu_bench_baseline --size=4194304 --block-size=65536 --iterations=7 --dataset-dir=build/empty-datasets --dataset=english
build/pivco/pivco_gpu_bench --size=4194304 --block-size=65536 --iterations=7 --dataset-dir=build/empty-datasets --dataset=english
```

## Author note

This was entirely vibe-coded by an LLM. I am not in a position to tell you if this is a useful contribution, unfortunately. Exercise caution accordingly.
