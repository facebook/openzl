# PivCo-Huffman

This is an implementation of PivCo-Huffman coding from Marcin Zukowski.
The code is based on ideas from:

- [The paper](https://arxiv.org/abs/2606.05765)
- [The repo](https://github.com/MarcinZukowski/pivco-huffman)
- [Ryg's blog](https://fgiesen.wordpress.com/2026/06/21/pivco-huffman-merge-operations/)

OpenZL re-implements PivCo-Huffman instead of using the upstream repo for several reasons:

1. OpenZL needs full control over the wire-format.
2. OpenZL needs to be hardened against decoding corrupted data.
3. OpenZL needs to be able to dynamically dispatch to kernels based on the CPUID,
e.g. to detect AVX512 support at runtime.

The goal is to work with upstream to factor out the primitives so that OpenZL can
import and use upstream's primitives, as those are the most complex part of PivCo,
and are independent of the wire format.

In the meantime, any meaningful improvements will be upstreamed, so there is a
single reference implementation for PivCo Huffman.
