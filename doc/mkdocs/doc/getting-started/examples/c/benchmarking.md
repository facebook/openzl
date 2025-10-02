# Lighweight Benchmarking
A lightweight benchmarking tool `unitBench` is provided in the codebase as a helper to benchmark some common compression use cases.

## Build and Use
```bash
make unitBench
```
The `unitBench` binary expects a scenario and some number of input files. Here is a sample command to benchmark Zstd compression on a few files
```bash
./unitBench zstdDirect file1.txt file2.txt file3.txt
```
A full list of existing scenarios can be found with the `--list` option. Use `-h` to learn more about other options.

## Creating a custom benchmark
The `unitBench` tool is designed to be easily extensible. To create a new benchmark, simply append a scenario to the scenario list here:
```cpp title="benchmark/unitBench/benchList.h"
--8<-- "src/benchmark/unitBench/benchList.h:scenario-list"
```
Each scenario is a struct containing a scenario name and some number of user-defined function pointers.
::: Bench_Entry
There are 2 ways to declare a benchmarked function. The first one is to pass a graph function (a "standard" scenario).
```cpp title="benchmark/unitBench/benchList.h"
--8<-- "src/benchmark/unitBench/benchList.h:example-fn"

// scenario definition
--8<-- "src/benchmark/unitBench/benchList.h:example-fn-scenario"
```
If the scenario is not representable as a graph, it is still benchmarkable, but will require some extra work. Directly declare the function to be tested (a "custom" scenario):
::: BMK_benchFn_t
```cpp title="benchmark/unitBench/scenarios/codecs/estimate_scenario.c"
--8<-- "src/benchmark/unitBench/scenarios/codecs/estimate_scenario.c:custom-wrapper"
```
```cpp title="benchmark/unitBench/benchList.h"
// scenario definition
--8<-- "src/benchmark/unitBench/benchList.h:example-wrapper-scenario"
```
This scenario also declares the `outSize` function. This tells `unitBench` how much space to allocate for the compressed output. `out_identical` is a convenience funtion meaning "allocate a buffer with the same size as the input".
::: BMK_outSize_f
```cpp title="benchmark/unitBench/benchList.h"
--8<-- "src/benchmark/unitBench/benchList.h:out-identical"
```

## Advanced scenario configuration
### Prep
::: BMK_prepFn_t
The `prep` function is an optional pre-processing function that is called on the input buffer. It can be used to massage the input if the scenario has special expectations on the input.
```cpp title="benchmark/unitBench/scenarios/codecs/dispatch_by_tag.c"
--8<-- "src/benchmark/unitBench/scenarios/codecs/dispatch_by_tag.c:splitBy8_preparation"
```

### Display
::: BMK_display_f
The `display` function is an optional function to calculate and print benchmark results in a format that differs from the standard format. Typically, this will be defined if special calculations need to be done to accurately calculate size or speed. For instance, decompression benchmarks need to use the generated size and not the source size when calculate speed.
```cpp title="benchmark/unitBench/benchList.h"
--8<-- "src/benchmark/unitBench/benchList.h:decoder-display"
```
