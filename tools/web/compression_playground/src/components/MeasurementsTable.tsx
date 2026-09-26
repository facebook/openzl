// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Fragment, useEffect, useState, type ReactNode} from 'react';
import {Box, Text, chakra} from '@chakra-ui/react';
import {LuChevronDown, LuChevronRight} from 'react-icons/lu';
import type {CompressorName} from '../compressors.ts';
import type {JobResult, RunState} from '../benchmarkTypes.ts';
import {
  barFraction,
  formatBytes,
  formatRatio,
  formatRange,
  formatRatioRange,
  formatSpeed,
  formatSpeedRange,
  frontierTag,
  groupMeasurements,
  peaksOf,
  rowDetail,
  resultsOf,
  sortGroups,
  type FrontierTag,
  type MeasurementGroup,
  type Peaks,
  type SortDirection,
  type SortKey,
} from '../measurements.ts';
import {CODEC_COLOR} from '../theme.ts';

/**
 * Shared by the header and every row, so the columns line up without a grid.
 * Widths are shares rather than pixels: `table-layout: fixed` gives a column
 * exactly the width it asks for and lets the table overflow its container when
 * the total is too wide, which is how this first rendered outside the results
 * card. Below `TABLE_MIN_WIDTH` the wrapper scrolls instead of shrinking them
 * into each other.
 */
const COLUMNS = [
  {key: 'codec', label: 'CODEC', width: '15.5%', sortable: false},
  {key: 'detail', label: 'LEVEL / PROFILE', width: '15.5%', sortable: true},
  {key: 'compressedSize', label: 'COMPRESSED', width: '14%', sortable: true},
  {key: 'ratio', label: 'RATIO', width: '20%', sortable: true},
  {key: 'compressMBps', label: 'COMPRESS', width: '17.5%', sortable: true},
  {key: 'decompressMBps', label: 'DECOMPRESS', width: '17.5%', sortable: true},
] as const;

// The frame's own columns come to 830px, and the group rows carry ranges like
// `15.7 KB - 23.9 KB` that collide with the next cell below that.
const TABLE_MIN_WIDTH = '830px';

const TAG_COLOR: Record<FrontierTag, {bg: string; fg: string}> = {
  'max ratio': {bg: 'pg.tagRatioBg', fg: 'pg.tagRatioFg'},
  balanced: {bg: 'pg.tagBalancedBg', fg: 'pg.tagBalancedFg'},
  'max speed': {bg: 'pg.tagSpeedBg', fg: 'pg.tagSpeedFg'},
};

function SortArrow({active, direction}: {active: boolean; direction: SortDirection}) {
  return (
    <Box as="span" aria-hidden="true" display="inline-flex" flexDirection="column" lineHeight="0.6" fontSize="8px">
      <Box as="span" color={active && direction === 'asc' ? 'pg.ink' : 'pg.border'}>
        ▲
      </Box>
      <Box as="span" color={active && direction === 'desc' ? 'pg.ink' : 'pg.border'}>
        ▼
      </Box>
    </Box>
  );
}

/** A value with the share of the run's largest it represents drawn behind it. */
function BarCell({
  value,
  peak,
  color,
  label,
  labelColor,
  trackWidth,
}: {
  value: number;
  peak: number;
  color: string;
  label: string;
  labelColor: string;
  trackWidth: string;
}) {
  return (
    <Box display="flex" alignItems="center" gap="8px">
      <Box
        aria-hidden="true"
        width={trackWidth}
        height="8px"
        flexShrink={0}
        borderRadius="2px"
        bg="pg.barTrack"
        overflow="hidden">
        <Box width={`${String(barFraction(value, peak) * 100)}%`} height="100%" bg={color} />
      </Box>
      <Text fontSize="13px" color={labelColor} m={0} whiteSpace="nowrap">
        {label}
      </Text>
    </Box>
  );
}

/**
 * The first column of a detail row: which of the group's rows this is. A
 * levelled row has nothing to put here -- the group above names the codec and
 * the next column carries the level -- and the design leaves it empty.
 */
function CandidateCell({result}: {result: JobResult}) {
  const candidate = result.candidate;
  if (candidate === null) {
    return null;
  }
  const tag = frontierTag(candidate.index, candidate.total);
  return (
    <>
      <Text fontSize="12px" color="pg.faint" m={0}>
        #{candidate.index}
      </Text>
      {tag !== null && (
        <Box
          px="6px"
          py="2px"
          borderRadius="4px"
          bg={TAG_COLOR[tag].bg}
          color={TAG_COLOR[tag].fg}
          fontSize="11px"
          fontWeight="semibold"
          whiteSpace="nowrap">
          {tag}
        </Box>
      )}
    </>
  );
}

function CodecLabel({compressor}: {compressor: CompressorName}) {
  return (
    <>
      <Box aria-hidden="true" boxSize="8px" borderRadius="full" bg={CODEC_COLOR[compressor]} flexShrink={0} />
      <Text fontSize="14px" fontWeight="bold" color="pg.ink" m={0}>
        {compressor}
      </Text>
    </>
  );
}

/** One measurement and its bars. `lead` is what the codec column carries. */
function MeasurementRow({
  row,
  color,
  peaks,
  lead,
  indent,
}: {
  row: JobResult;
  color: string;
  peaks: Peaks;
  lead: ReactNode;
  indent: boolean;
}) {
  // The padding is on every cell rather than the first: a levelled row leaves
  // the codec column empty, so a row padded only there collapses to the
  // padding and sits half the height of the group row above it.
  const cell = {as: 'td', px: '12px', py: '10px', borderBottomWidth: '1px', borderColor: 'pg.border'} as const;
  return (
    <Box as="tr">
      <Box {...cell} pl={indent ? '28px' : '12px'}>
        <Box display="flex" alignItems="center" gap="8px">
          {lead}
        </Box>
      </Box>
      <Box {...cell}>
        <Text fontSize="12px" color="pg.muted" m={0}>
          {rowDetail(row)}
        </Text>
      </Box>
      <Box {...cell}>
        <Text fontSize="13px" color="pg.ink" m={0} whiteSpace="nowrap">
          {formatBytes(row.compressedSize)}
        </Text>
      </Box>
      <Box {...cell}>
        <BarCell
          value={row.ratio}
          peak={peaks.ratio}
          color={color}
          label={formatRatio(row.ratio)}
          labelColor="pg.ink"
          trackWidth="60px"
        />
      </Box>
      <Box {...cell}>
        <BarCell
          value={row.compressMBps}
          peak={peaks.compressMBps}
          color="pg.barCompress"
          label={formatSpeed(row.compressMBps)}
          labelColor="pg.secondary"
          trackWidth="40px"
        />
      </Box>
      <Box {...cell}>
        <BarCell
          value={row.decompressMBps}
          peak={peaks.decompressMBps}
          color="pg.barDecompress"
          label={formatSpeed(row.decompressMBps)}
          labelColor="pg.secondary"
          trackWidth="40px"
        />
      </Box>
    </Box>
  );
}

function GroupHeader({group, expanded, onToggle}: {group: MeasurementGroup; expanded: boolean; onToggle: () => void}) {
  // What the group spans in each column, which is what a collapsed group has
  // to say on its own.
  const cells = [
    formatRange(
      group.rows.map((row) => row.compressedSize),
      formatBytes,
    ),
    formatRatioRange(group.rows.map((row) => row.ratio)),
    formatSpeedRange(group.rows.map((row) => row.compressMBps)),
    formatSpeedRange(group.rows.map((row) => row.decompressMBps)),
  ];

  return (
    <Box as="tr" bg="pg.groupRow">
      <Box as="td" px="12px" py="10px" borderBottomWidth="1px" borderColor="pg.border">
        {/* The control is the cell's button rather than the row: a `tr` cannot
            carry `aria-expanded` meaningfully, and a row-level handler would
            fire alongside this one. */}
        <chakra.button
          type="button"
          display="flex"
          alignItems="center"
          gap="8px"
          aria-expanded={expanded}
          // The detail as well as the codec: two rows can run the same one, and
          // `Expand zstd` twice leaves the two buttons indistinguishable.
          aria-label={`${expanded ? 'Collapse' : 'Expand'} ${group.compressor}, ${group.detail}`}
          onClick={onToggle}>
          <Box as="span" aria-hidden="true" display="inline-flex" color={CODEC_COLOR[group.compressor]}>
            {expanded ? <LuChevronDown size={14} /> : <LuChevronRight size={14} />}
          </Box>
          <CodecLabel compressor={group.compressor} />
        </chakra.button>
      </Box>
      <Box as="td" px="12px" py="10px" borderBottomWidth="1px" borderColor="pg.border">
        <Text fontSize="11px" color="pg.muted" m={0}>
          {group.detail}
        </Text>
      </Box>
      {cells.map((cell, index) => (
        <Box as="td" key={COLUMNS[index + 2].key} px="12px" py="10px" borderBottomWidth="1px" borderColor="pg.border">
          <Text fontSize="13px" color="pg.secondary" m={0} whiteSpace="nowrap">
            {cell}
          </Text>
        </Box>
      ))}
    </Box>
  );
}

export default function MeasurementsTable({runState}: {runState: RunState}) {
  const [sort, setSort] = useState<{key: SortKey; direction: SortDirection}>({key: 'codec', direction: 'desc'});
  // Every group opens with the run: what it measured is the answer, and a
  // summary row standing in for rows already on screen is a click between the
  // reader and the numbers. This holds the ones they have shut since.
  const [collapsed, setCollapsed] = useState<ReadonlySet<number>>(new Set());

  // A new run opens them again. Row ids are reused between runs, so without
  // this the last run's collapses are still applied to groups they were never
  // made for.
  useEffect(() => {
    if (runState.status === 'loading') {
      setCollapsed(new Set());
    }
  }, [runState.status]);

  const results = resultsOf(runState);
  if (results.length === 0) {
    return null;
  }
  const peaks: Peaks = peaksOf(results);
  const groups = sortGroups(groupMeasurements(results), sort.key, sort.direction);

  const toggleSort = (key: SortKey) => {
    setSort((current) =>
      current.key === key ? {key, direction: current.direction === 'desc' ? 'asc' : 'desc'} : {key, direction: 'desc'},
    );
  };

  return (
    <Box display="flex" flexDirection="column" gap="16px" pt="12px">
      <Text
        id="measurements-title"
        color="pg.faint"
        fontSize="11px"
        fontWeight="extrabold"
        textTransform="uppercase"
        m={0}>
        Measurements
      </Text>
      <Box overflowX="auto">
        {/* Named by the heading above rather than a second copy of the word,
            so the two cannot drift apart. */}
        <Box
          as="table"
          aria-labelledby="measurements-title"
          width="100%"
          minW={TABLE_MIN_WIDTH}
          borderCollapse="collapse"
          style={{tableLayout: 'fixed'}}>
          <Box as="thead">
            <Box as="tr">
              {COLUMNS.map((column) => (
                // `chakra.th` rather than `Box as="th"`: the polymorphic `as`
                // keeps a div's props, and `scope` is not one of them.
                <chakra.th
                  scope="col"
                  key={column.key}
                  width={column.width}
                  px="12px"
                  pb="8px"
                  textAlign="left"
                  borderBottomWidth="1px"
                  borderColor="pg.border">
                  {column.sortable ? (
                    <chakra.button
                      type="button"
                      display="inline-flex"
                      alignItems="center"
                      gap="4px"
                      color="pg.faint"
                      fontSize="11px"
                      fontWeight="semibold"
                      textTransform="uppercase"
                      aria-label={`Sort by ${column.label.toLowerCase()}`}
                      onClick={() => {
                        toggleSort(column.key);
                      }}>
                      {column.label}
                      <SortArrow active={sort.key === column.key} direction={sort.direction} />
                    </chakra.button>
                  ) : (
                    <Text color="pg.faint" fontSize="11px" fontWeight="semibold" textTransform="uppercase" m={0}>
                      {column.label}
                    </Text>
                  )}
                </chakra.th>
              ))}
            </Box>
          </Box>
          <Box as="tbody">
            {groups.map((group) => {
              const color = CODEC_COLOR[group.compressor];
              // A group of one has nothing to summarise: its header would print
              // the same four numbers as the row under it.
              if (group.rows.length === 1) {
                return (
                  <MeasurementRow
                    key={group.rowId}
                    row={group.rows[0]}
                    color={color}
                    peaks={peaks}
                    indent={false}
                    lead={<CodecLabel compressor={group.compressor} />}
                  />
                );
              }
              const expanded = !collapsed.has(group.rowId);
              return (
                <Fragment key={group.rowId}>
                  <GroupHeader
                    group={group}
                    expanded={expanded}
                    onToggle={() => {
                      setCollapsed((current) => {
                        const next = new Set(current);
                        if (!next.delete(group.rowId)) {
                          next.add(group.rowId);
                        }
                        return next;
                      });
                    }}
                  />
                  {expanded &&
                    group.rows.map((row) => (
                      <MeasurementRow
                        key={`${row.job.id}-${String(row.candidate?.index ?? 0)}`}
                        row={row}
                        color={color}
                        peaks={peaks}
                        indent
                        lead={<CandidateCell result={row} />}
                      />
                    ))}
                </Fragment>
              );
            })}
          </Box>
        </Box>
      </Box>
      <Text color="pg.faint" fontSize="12px" lineHeight="1.4" px="4px" m={0}>
        A point is on the frontier when nothing else beats it on ratio, compression speed and decompression speed at the
        same time.
      </Text>
    </Box>
  );
}
