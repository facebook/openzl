// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Heading, Text, VStack, useToken} from '@chakra-ui/react';
import {CartesianGrid, ResponsiveContainer, Scatter, ScatterChart, Tooltip, XAxis, YAxis} from 'recharts';
import type {CompressorName} from '../compressors.ts';
import type {JobResult} from '../benchmarkTypes.ts';
import {
  buildSeries,
  formatRatioTick,
  formatSpeedTick,
  logDomain,
  logTicks,
  type ChartPoint,
  type SpeedAxis,
} from '../charts.ts';
import {formatRatio, formatSpeed} from '../measurements.ts';
import {CODEC_COLOR} from '../theme.ts';

/**
 * recharts hands `fill` and `stroke` to SVG, which cannot resolve a token
 * name. A semantic token with dark variants resolves to its CSS variable
 * rather than to a value, though, and SVG does take one of those -- so the
 * charts still follow the scheme instead of carrying their own hex.
 */
function useChartColors() {
  const [openzl, zstd, gzip, line, label] = useToken('colors', [
    CODEC_COLOR.OpenZL,
    CODEC_COLOR.zstd,
    CODEC_COLOR.gzip,
    'pg.border',
    'pg.faint',
  ]);
  return {codec: {OpenZL: openzl, zstd, gzip} satisfies Record<CompressorName, string>, line, label};
}

const AXES = [
  {
    axis: 'compressMBps',
    title: 'Compression',
    subtitle: 'Ratio against compression throughput',
    xLabel: 'Compress MB/s (log)',
  },
  {
    axis: 'decompressMBps',
    title: 'Decompression',
    subtitle: 'Ratio against decompression throughput',
    xLabel: 'Decompress MB/s (log)',
  },
] as const satisfies readonly {axis: SpeedAxis; title: string; subtitle: string; xLabel: string}[];

function PointTooltip({active, payload}: {active?: boolean; payload?: {payload: ChartPoint}[]}) {
  const point = payload?.[0]?.payload;
  if (active !== true || point === undefined) {
    return null;
  }
  return (
    <Box bg="pg.surface" borderWidth="1px" borderColor="pg.border" borderRadius="6px" px="10px" py="6px" boxShadow="sm">
      <Text fontSize="12px" fontWeight="bold" color="pg.ink" m={0}>
        {point.label}
      </Text>
      <Text fontSize="12px" color="pg.secondary" m={0} whiteSpace="nowrap">
        {formatRatio(point.y)} at {formatSpeed(point.x)}
      </Text>
    </Box>
  );
}

function Chart({results, axis, xLabel}: {results: readonly JobResult[]; axis: SpeedAxis; xLabel: string}) {
  const {codec, line, label} = useChartColors();
  const tickStyle = {fill: label, fontSize: 11};
  const series = buildSeries(results, axis);
  const xDomain = logDomain(results.map((result) => result[axis]));
  const yDomain = logDomain(results.map((result) => result.ratio));

  return (
    <Box display="flex" alignItems="stretch" gap="2px">
      <Text
        fontSize="10px"
        color="pg.faint"
        m={0}
        alignSelf="center"
        whiteSpace="nowrap"
        style={{writingMode: 'vertical-rl', transform: 'rotate(180deg)'}}>
        Compression ratio (log)
      </Text>
      <Box flex="1" minW={0}>
        <Box height="200px" width="100%">
          <ResponsiveContainer width="100%" height="100%">
            <ScatterChart margin={{top: 8, right: 12, bottom: 4, left: 0}}>
              <CartesianGrid stroke={line} strokeDasharray="3 3" />
              <XAxis
                type="number"
                dataKey="x"
                scale="log"
                domain={[...xDomain]}
                ticks={[...logTicks(xDomain)]}
                tickFormatter={formatSpeedTick}
                tick={tickStyle}
                stroke={line}
              />
              <YAxis
                type="number"
                dataKey="y"
                scale="log"
                domain={[...yDomain]}
                ticks={[...logTicks(yDomain)]}
                tickFormatter={formatRatioTick}
                tick={tickStyle}
                stroke={line}
                width={44}
              />
              <Tooltip content={<PointTooltip />} cursor={{strokeDasharray: '3 3'}} />
              {series.map((one) => (
                <Scatter
                  key={one.compressor}
                  name={one.compressor}
                  data={[...one.points]}
                  fill={codec[one.compressor]}
                  line={{stroke: codec[one.compressor], strokeWidth: 1.5}}
                />
              ))}
            </ScatterChart>
          </ResponsiveContainer>
        </Box>
        <Text fontSize="10px" color="pg.faint" textAlign="center" m={0}>
          {xLabel}
        </Text>
      </Box>
    </Box>
  );
}

function LegendChip({compressor, count}: {compressor: CompressorName; count: number}) {
  // The chip is Chakra rather than SVG, so the token name goes in directly and
  // `/n` washes it out to n percent.
  const color = CODEC_COLOR[compressor];
  return (
    <Box
      display="flex"
      alignItems="center"
      gap="6px"
      pl="10px"
      pr="12px"
      py="6px"
      borderRadius="100px"
      borderWidth="1.5px"
      borderColor={`${color}/35`}
      bg={`${color}/8`}>
      <Box aria-hidden="true" boxSize="10px" borderRadius="full" bg={color} flexShrink={0} />
      <Text fontSize="12px" fontWeight="medium" color="pg.ink" m={0}>
        {compressor}
      </Text>
      <Box px="6px" py="1px" borderRadius="100px" bg={`${color}/15`} color={color} fontSize="11px" fontWeight="bold">
        {count}
      </Box>
    </Box>
  );
}

export default function RatioSpeedCharts({results}: {results: readonly JobResult[]}) {
  if (results.length === 0) {
    return null;
  }
  // Both charts hold the same points, so either one's series gives the counts.
  const series = buildSeries(results, 'compressMBps');

  return (
    <VStack gap="16px" align="stretch">
      <VStack gap="4px" align="stretch">
        <Heading
          as="h3"
          color="pg.faint"
          fontSize="11px"
          fontWeight="extrabold"
          textTransform="uppercase"
          letterSpacing="0.02em"
          m={0}>
          Ratio vs. speed
        </Heading>
        <Text color="pg.secondary" fontSize="13px" m={0}>
          Higher and further right is better · hover to get more details
        </Text>
      </VStack>

      <Box display="flex" flexDirection={{base: 'column', md: 'row'}} gap="16px" alignItems="stretch">
        {AXES.map((chart) => (
          <Box
            key={chart.axis}
            flex="1"
            minW={0}
            bg="pg.surface"
            borderWidth="1px"
            borderColor="pg.border"
            borderRadius="8px"
            p="20px">
            <VStack gap="12px" align="stretch">
              <VStack gap="2px" align="stretch">
                <Text fontSize="14px" fontWeight="bold" color="pg.ink" m={0}>
                  {chart.title}
                </Text>
                <Text fontSize="12px" color="pg.secondary" m={0}>
                  {chart.subtitle}
                </Text>
              </VStack>
              <Chart results={results} axis={chart.axis} xLabel={chart.xLabel} />
            </VStack>
          </Box>
        ))}
      </Box>

      <Box display="flex" flexWrap="wrap" gap="8px">
        {series.map((one) => (
          <LegendChip key={one.compressor} compressor={one.compressor} count={one.points.length} />
        ))}
      </Box>
    </VStack>
  );
}
