// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useRef, useState} from 'react';
import {
  createCompressorRow,
  type CompressorName,
  type CompressorRow,
  type CompressorRowBase,
  type OpenZlRow,
} from './compressors.ts';

function defaultRows(): readonly CompressorRow[] {
  return [createCompressorRow(1, 'OpenZL'), createCompressorRow(2, 'zstd'), createCompressorRow(3, 'gzip')];
}

/**
 * Split in two because `Partial<CompressorRow>` over a union distributes into
 * `Partial<OpenZlRow> | Partial<ZstdRow> | ...`, which no longer spreads back
 * into a row without a cast. Keeping them separate also says which fields any
 * row has and which only an OpenZL row does.
 *
 * Neither carries `id`: that is the argument selecting the row, so a patch
 * holding one could only ever disagree with it.
 */
export type PatchRow = (id: number, patch: Omit<Partial<CompressorRowBase>, 'id'>) => void;
export type PatchOpenZlRow = (id: number, patch: Omit<Partial<OpenZlRow>, 'id'>) => void;

export interface CompressorRows {
  readonly rows: readonly CompressorRow[];
  readonly addRow: () => void;
  readonly removeRow: (id: number) => void;
  readonly patchRow: PatchRow;
  readonly patchOpenZlRow: PatchOpenZlRow;
  readonly changeCompressor: (id: number, compressor: CompressorName) => void;
}

/**
 * Owns the compressor rows and hands out their ids. Every way to change a row
 * lives here rather than in the card that renders them, so the state and the
 * rules for moving it stay together and can be tested without mounting any UI.
 *
 * Ids never repeat within a session: `rowId` travels into `BenchmarkJob` and
 * comes back on its result, so reusing a removed row's id would attach a
 * finished measurement to whatever row took its place. The counter therefore
 * starts above the highest id it was seeded with and only ever climbs.
 */
export function useCompressorRows(initialRows?: readonly CompressorRow[]): CompressorRows {
  const [rows, setRows] = useState<readonly CompressorRow[]>(() => initialRows ?? defaultRows());
  // Seeded with 0 because `Math.max()` over an empty list is -Infinity.
  const nextId = useRef(Math.max(0, ...rows.map((row) => row.id)) + 1);

  return {
    rows,
    addRow: () => {
      const id = nextId.current;
      nextId.current += 1;
      setRows((current) => [...current, createCompressorRow(id, 'OpenZL')]);
    },
    removeRow: (id: number) => {
      setRows((current) => current.filter((row) => row.id !== id));
    },
    patchRow: (id, patch) => {
      setRows((current) => current.map((row) => (row.id === id ? {...row, ...patch} : row)));
    },
    // Narrows before spreading, so the OpenZL-only fields can only ever land
    // on a row that actually has them.
    patchOpenZlRow: (id, patch) => {
      setRows((current) =>
        current.map((row) => (row.id === id && row.compressor === 'OpenZL' ? {...row, ...patch} : row)),
      );
    },
    changeCompressor: (id: number, compressor: CompressorName) => {
      // A fresh row resets the dependent fields: a zstd level of 19 is not a
      // valid OpenZL level, and a profile means nothing to gzip.
      setRows((current) => current.map((row) => (row.id === id ? createCompressorRow(id, compressor) : row)));
    },
  };
}
