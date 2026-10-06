// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Text, useToken} from '@chakra-ui/react';
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

/** The plot itself, and the only part of the charts that needs recharts. */
export default function ScatterPlot({results, axis}: {results: readonly JobResult[]; axis: SpeedAxis}) {
  const {codec, line, label} = useChartColors();
  const tickStyle = {fill: label, fontSize: 11};
  const series = buildSeries(results, axis);
  const xDomain = logDomain(results.map((result) => result[axis]));
  const yDomain = logDomain(results.map((result) => result.ratio));

  return (
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
  );
}
