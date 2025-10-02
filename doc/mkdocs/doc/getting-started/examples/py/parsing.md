## Installation

From PyPI:

```
pip install openzl
```

From source:

```
git clone https://github.com/facebook/openzl
cd openzl/py
pip install .
```

## Running the example

This example is self-contained and can be run after installing OpenZL and NumPy.
The full source can be found in `examples/py/parsing.py`.

```
python3 examples/py/parsing.py
```

## Imports

```python
--8<-- "src/examples/py/parsing.py:imports"
```

## Generate test data

We'll generate simple data in the following format.

```
[4-byte number of elements]
[1-byte element width]
[(number-of-elements * element-width) bytes of data]
...
```

It is a trivial example, but it is enough to show that you can improve compression by parsing the data, especially if the data is numeric.

```python
--8<-- "src/examples/py/parsing.py:generate_test_data"
```

## Setting up the parser

OpenZL allows constructing graphs at runtime, similar to the [Selector][openzl.ext.Selector] that we saw in the [quick start example](./quick-start.md), but more powerful.
This is done through [`FunctionGraph`s][openzl.ext.FunctionGraph].
Inside a function graph you can:

* Inspect the input edge, and the data attached to that edge.
* Run a node on any edge, and get the resulting edges back.
* Send an edge to a destination graph.
* Inspect parameters.
* Try compression with [tryGraph][openzl.ext.GraphState.tryGraph] and see the compressed size.

At the end of a function graph, every incoming edge, and newly created edge has to be consumed by running a node on it, or sending it to a destination graph.

In this example, we'll be parsing the input and separating data sections by their element width.
Then we'll convert the outputs into numeric data, and compress them using OpenZL's generic numeric compression graph.

```python
--8<-- "src/examples/py/parsing.py:parsing_function_graph"
```

## Set up the compressor

All you have to do is register the `ParsingFunctionGraph` with the compressor.
This gives you a [GraphID][openzl.ext.GraphID] that can be used as the starting graph, or as a component in a larger graph.

```python
--8<-- "src/examples/py/parsing.py:compressor"
```

## Putting it all together

This all looks very similar to the [quick start example](./quick-start.md).

```python
--8<-- "src/examples/py/parsing.py:measurement"
```

In this trivial example, we get an immediate boost by parsing the input, and handling it as numeric data.

```
Zstd compressor ratio: 2.98
Parsing compressor ratio: 3.27
```
