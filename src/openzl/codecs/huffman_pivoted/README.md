# Huffman Pivoted

This is an implementation of PivCo-Huffman coding from Marcin Zukowski.
The code is based on ideas from:

- [The paper](https://arxiv.org/abs/2606.05765)
- [The repo](https://github.com/MarcinZukowski/pivco-huffman)
- [Ryg's blog](https://fgiesen.wordpress.com/2026/06/21/pivco-huffman-merge-operations/)

OpenZL re-implements PivCo-Huffman instead of using the upstream repo for several reasons:

1. OpenZL needs full control over the wire-format.
2. Upstream is currently not hardened against decoding corrupted data.
3. The upstream repo isn't currently set-up to be imported into OpenZL, and includes code that is only needed for benchmarking.
