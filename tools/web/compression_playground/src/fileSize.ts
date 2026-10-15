// Copyright (c) Meta Platforms, Inc. and affiliates.

const FILE_SIZE_UNITS = ['B', 'KB', 'MB', 'GB', 'TB'] as const;

/**
 * Decimal units, not binary ones, so the size the sample button promises reads
 * back the same in the drop zone once the sample is chosen.
 */
export function formatFileSize(bytes: number): string {
  let size = bytes;
  let unit = 0;
  while (size >= 1000 && unit < FILE_SIZE_UNITS.length - 1) {
    size /= 1000;
    unit += 1;
  }
  const rounded = unit === 0 || size >= 10 ? Math.round(size) : Math.round(size * 10) / 10;
  return `${rounded} ${FILE_SIZE_UNITS[unit]}`;
}
