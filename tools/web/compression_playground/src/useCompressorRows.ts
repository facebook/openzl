// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useRef, useState, type Dispatch, type SetStateAction} from 'react';
import {createCompressorRow, type CompressorRow} from './compressors.ts';

function defaultRows(): readonly CompressorRow[] {
  return [createCompressorRow(1, 'OpenZL'), createCompressorRow(2, 'zstd'), createCompressorRow(3, 'gzip')];
}

export interface CompressorRows {
  readonly rows: readonly CompressorRow[];
  /** Takes React's updater form too, so callers can patch off the latest rows. */
  readonly setRows: Dispatch<SetStateAction<readonly CompressorRow[]>>;
  readonly addRow: () => void;
  readonly removeRow: (id: number) => void;
}

/**
 * Owns the compressor rows and hands out their ids.
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
    setRows,
    addRow: () => {
      const id = nextId.current;
      nextId.current += 1;
      setRows((current) => [...current, createCompressorRow(id, 'OpenZL')]);
    },
    removeRow: (id: number) => {
      setRows((current) => current.filter((row) => row.id !== id));
    },
  };
}
