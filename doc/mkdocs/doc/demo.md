# OpenZL Demo

This demo shows how to run OpenZL training to produce a [Pareto frontier](https://en.wikipedia.org/wiki/Pareto_front) of OpenZL compressors that tradeoff compression ratio, compression speed, and decompression speed.

First, install the demo package:

```sh
pip install openzl-demo
```

Then, pick some data to train with, and run the trainer.
It should be a few MB of `bytes` containing numeric data.
E.g. training data, embeddings, or model weights.
Any data will work, but if there isn't a simple structure this demo trainer likely won't perform well.

```python
import openzl_demo

DATA = b"replace with sample data"

results = openzl_demo.train([DATA])
```

After running the trainer, there are several things you can do with the `results`.
First, you can visualize the results:

```python
results.plot().write_html("plot.html")
```

--8<-- "doc/assets/openzl-demo-int64-plot.html"

Each point is a different OpenZL compressor.
If you find a tradeoff that is interesting, e.g. maybe point 1, you can grab the serialized representation of that compressor:

```python
compressor = result.compressors[1]
```

Given that compressor, you can run compression & decompression:

```python
compressed = openzl_demo.compress(compressor, DATA)
decompressed = openzl_demo.decompress(compressed)
assert DATA == decompressed
```

Note that the decompression is independent of the compressor that was used.
The compression graph that the `compressor` specified is written into the compressed data, so the same universal decompressor can always decode the data.

Additionally, a benchmarking utility is provided to measure compression ratio, compression speed, and decompression speed:

```python
print(openzl_demo.benchmark(compressor, [DATA]))
```

Finally, this is all compatible with the `openzl` package and [CLI](./getting-started/cli.md):


```python
with open("compressor.zlc", "wb") as f:
    f.write(compressor)
```

```sh
./zli compress -c compressor.zlc INPUT -o output.zc
./zli decompress output.zc -o decompressed

./zli benchmark -c compressor.zlc INPUT
```

Check out our [quick start guide](./getting-started/quick-start.md) to learn more!
