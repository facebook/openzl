# Compression Transformer selector

This directory contains OpenZL's runtime integration of the Compression
Transformer numeric selector. Model training, evaluation, and scorer generation
belong in the upstream training repository; changes that can affect feature
values or model decisions must be validated there before being synchronized
here.

Generated scorer snapshots live in `generated/`; the surrounding files are
the hand-written OpenZL runtime integration.

## Source provenance

The generated scorers are promoted from the `max_compression` scenario in
[CompressionTransformer2](https://github.com/Cyan4973/CompressionTransformer2).
Record the exact upstream revision and model artifact provenance in the diff
that imports each update. These values change with model promotion and are not
maintained as permanent metadata in this README.

## Regenerating upstream scorers

From the validated upstream revision selected for import, regenerate each
runtime scorer with:

```sh
python3 training/py_core/export_to_c.py \
  --scenario max_compression \
  --context <num8|num16|num32|num64> \
  --runtime-output-dir runtime/generic_numeric
```

Production model promotion normally uses
`training/promote_model.py`, which validates a candidate before replacing the
checked-in model and scorer.

## OpenZL adaptation

The upstream generated scorer is a starting point rather than a file copied
verbatim. The OpenZL integration currently adds:

- `TRS_` prefixes for internal symbols;
- OpenZL include paths and assertion macros;
- operation-ID tables used by typed runtime dispatch; and
- the OpenZL feature extraction, workspace, and compatibility interfaces.

The upstream generator does not yet emit every adaptation above. Preserve them
when importing a newly generated model, and run the fast/full feature and
decision parity tests after synchronization.

The dense-range score guard intentionally preserves the upstream runtime's
float-rounded `0.90f` cutoff. Replacing it with a double literal changes routing
at and just below 90% density; evaluate that change upstream before
synchronizing it here.
