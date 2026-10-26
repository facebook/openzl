// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Suspense, lazy, useState} from 'react';
import {Box, Heading, Text, VStack, chakra} from '@chakra-ui/react';
import type {CompressorName} from '../compressors.ts';
import type {JobResult} from '../benchmarkTypes.ts';
import {buildSeries, type SpeedAxis} from '../charts.ts';
import {CODEC_COLOR} from '../theme.ts';

/**
 * Only the plot waits on recharts, which stays out of the entry chunk since
 * nothing can be charted before a run produces something. Everything around
 * it renders at once at its final size, so nothing moves when the plot lands.
 */
const ScatterPlot = lazy(() => import('./ScatterPlot.tsx'));

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

interface ChartProps {
  results: readonly JobResult[];
  axis: SpeedAxis;
  xLabel: string;
  highlighted: string | null;
  hidden: ReadonlySet<CompressorName>;
}

function Chart({results, axis, xLabel, highlighted, hidden}: ChartProps) {
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
          {/* recharts drops the tick labels once no series is left, which
              would leave an empty frame with nothing to say why. */}
          {results.every((result) => hidden.has(result.job.compressor)) ? (
            <Text
              height="100%"
              display="flex"
              alignItems="center"
              justifyContent="center"
              textAlign="center"
              color="pg.secondary"
              fontSize="13px"
              m={0}>
              Every codec is hidden. Click one below to show it.
            </Text>
          ) : (
            <Suspense fallback={null}>
              <ScatterPlot results={results} axis={axis} highlighted={highlighted} hidden={hidden} />
            </Suspense>
          )}
        </Box>
        <Text fontSize="10px" color="pg.faint" textAlign="center" m={0}>
          {xLabel}
        </Text>
      </Box>
    </Box>
  );
}

interface LegendChipProps {
  compressor: CompressorName;
  count: number;
  shown: boolean;
  onToggle: () => void;
}

/** Shows or hides the codec's points on both charts; the table keeps every row. */
function LegendChip({compressor, count, shown, onToggle}: LegendChipProps) {
  // The chip is Chakra rather than SVG, so the token name goes in directly and
  // `/n` washes it out to n percent.
  const color = CODEC_COLOR[compressor];
  return (
    <chakra.button
      type="button"
      aria-pressed={shown}
      onClick={onToggle}
      display="flex"
      alignItems="center"
      gap="6px"
      pl="10px"
      pr="12px"
      py="6px"
      borderRadius="100px"
      borderWidth="1.5px"
      borderColor={shown ? `${color}/35` : 'pg.border'}
      bg={shown ? `${color}/8` : 'transparent'}
      fontFamily="inherit"
      cursor="pointer"
      transition="background-color 150ms ease, border-color 150ms ease"
      _hover={{bg: shown ? `${color}/15` : 'pg.chip'}}
      _focusVisible={{outline: '2px solid', outlineColor: color, outlineOffset: '2px'}}>
      {/* Hollow when hidden, so the state does not rest on the colour alone. */}
      <Box
        aria-hidden="true"
        boxSize="10px"
        borderRadius="full"
        borderWidth="2px"
        borderColor={color}
        bg={shown ? color : 'transparent'}
        flexShrink={0}
      />
      <Text
        fontSize="12px"
        fontWeight="medium"
        color={shown ? 'pg.ink' : 'pg.muted'}
        textDecoration={shown ? 'none' : 'line-through'}
        m={0}>
        {compressor}
      </Text>
      <Box
        px="6px"
        py="1px"
        borderRadius="100px"
        bg={shown ? `${color}/15` : 'pg.chip'}
        color={shown ? color : 'pg.muted'}
        fontSize="11px"
        fontWeight="bold">
        {count}
      </Box>
    </chakra.button>
  );
}

/** `highlighted` is the `measurementKey` of the point to pick out on both charts. */
export default function RatioSpeedCharts({
  results,
  highlighted = null,
}: {
  results: readonly JobResult[];
  highlighted?: string | null;
}) {
  // Kept while the charts stay up, so a codec hidden mid-run stays hidden as
  // its points arrive.
  const [hidden, setHidden] = useState<ReadonlySet<CompressorName>>(new Set());
  if (results.length === 0) {
    return null;
  }
  const toggle = (compressor: CompressorName) => {
    setHidden((current) => {
      const next = new Set(current);
      if (!next.delete(compressor)) {
        next.add(compressor);
      }
      return next;
    });
  };
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
          Higher and further right is better · hover to get more details · click a codec below to hide or show it
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
              <Chart
                results={results}
                axis={chart.axis}
                xLabel={chart.xLabel}
                highlighted={highlighted}
                hidden={hidden}
              />
            </VStack>
          </Box>
        ))}
      </Box>

      <Box role="group" aria-label="Codecs shown on the charts" display="flex" flexWrap="wrap" gap="8px">
        {series.map((one) => (
          <LegendChip
            key={one.compressor}
            compressor={one.compressor}
            count={one.points.length}
            shown={!hidden.has(one.compressor)}
            onToggle={() => toggle(one.compressor)}
          />
        ))}
      </Box>
    </VStack>
  );
}
