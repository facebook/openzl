// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {afterEach, beforeAll, describe, expect, it} from 'vitest';
import {cleanup, render, screen, within} from '@testing-library/react';
import {ChakraProvider} from '@chakra-ui/react';
import RatioSpeedCharts from '../src/components/RatioSpeedCharts.tsx';
import {playgroundSystem} from '../src/theme.ts';
import type {BenchmarkJob, JobResult} from '../src/benchmarkTypes.ts';

/**
 * `ResponsiveContainer` measures its parent, and jsdom reports every element as
 * 0x0, so without this the charts render as an empty frame with no points.
 */
beforeAll(() => {
  Object.defineProperty(HTMLElement.prototype, 'clientWidth', {configurable: true, value: 400});
  Object.defineProperty(HTMLElement.prototype, 'clientHeight', {configurable: true, value: 200});
  Object.defineProperty(HTMLElement.prototype, 'getBoundingClientRect', {
    configurable: true,
    value: () => ({width: 400, height: 200, top: 0, left: 0, bottom: 200, right: 400, x: 0, y: 0}),
  });
});

afterEach(cleanup);

function result(
  job: Partial<BenchmarkJob> & Pick<BenchmarkJob, 'compressor'>,
  metrics: Partial<JobResult> = {},
): JobResult {
  return {
    iterations: 1,
    srcSize: 5_000_000,
    compressedSize: 100,
    ratio: 10,
    compressMs: 1,
    decompressMs: 1,
    compressMBps: 100,
    decompressMBps: 200,
    candidate: null,
    ...metrics,
    job: {id: 'j', rowId: 1, level: 1, iterations: 1, profile: 0, training: null, ...job} as BenchmarkJob,
  };
}

const RESULTS = [
  result({compressor: 'OpenZL', rowId: 1}, {candidate: {index: 1, total: 2}, ratio: 15235, compressMBps: 38}),
  result({compressor: 'OpenZL', rowId: 1}, {candidate: {index: 2, total: 2}, ratio: 2763, compressMBps: 310}),
  result({compressor: 'zstd', rowId: 2, level: 1}, {ratio: 1450, compressMBps: 520}),
  result({compressor: 'gzip', rowId: 3, level: 6}, {ratio: 342, compressMBps: 162}),
];

function renderCharts(results = RESULTS) {
  return render(
    <ChakraProvider value={playgroundSystem}>
      <RatioSpeedCharts results={results} />
    </ChakraProvider>,
  );
}

describe('RatioSpeedCharts', () => {
  it('renders nothing without measurements', () => {
    const {container} = renderCharts([]);
    expect(container).toBeEmptyDOMElement();
  });

  it('plots every measurement on both charts', () => {
    const {container} = renderCharts();
    // Two charts, four measurements each.
    expect(container.querySelectorAll('.recharts-scatter-symbol')).toHaveLength(8);
  });

  it('labels both axes as logarithmic and both throughputs', () => {
    renderCharts();
    expect(screen.getByText('Compress MB/s (log)')).toBeInTheDocument();
    expect(screen.getByText('Decompress MB/s (log)')).toBeInTheDocument();
    expect(screen.getAllByText('Compression ratio (log)')).toHaveLength(2);
  });

  it('counts each codec once in the legend, however many rows produced it', () => {
    renderCharts();
    const legend = screen.getByText('OpenZL').closest('div');
    expect(legend).not.toBeNull();
    expect(within(legend as HTMLElement).getByText('2')).toBeInTheDocument();
    expect(within(screen.getByText('gzip').closest('div') as HTMLElement).getByText('1')).toBeInTheDocument();
  });

  it('takes every colour from the theme, so the scheme can change them', () => {
    // recharts writes `fill` and `stroke` as SVG attributes, which is why the
    // colours arrive as CSS variables rather than as token names. A literal
    // hex anywhere here is one the dark scheme cannot reach.
    const {container} = renderCharts();
    const painted = [...container.querySelectorAll('[fill], [stroke]')].flatMap((node) => [
      node.getAttribute('fill'),
      node.getAttribute('stroke'),
    ]);

    expect(painted.filter((value) => value?.startsWith('#'))).toEqual([]);
  });

  it('places points on a log scale rather than a linear one', () => {
    const {container} = renderCharts();
    const xs = [...container.querySelectorAll('.recharts-scatter-symbol')]
      .slice(0, 4)
      .map((node) =>
        Number(/translate\(([\d.]+)/.exec(node.querySelector('path')?.getAttribute('transform') ?? '')?.[1]),
      );

    // Speeds are 38, 162, 310 and 520. Where 162 falls between 38 and 520 is
    // 26% on a linear axis and 55% on a log one -- (log162 - log38) over
    // (log520 - log38) -- and the domain's padding cancels out of that ratio.
    const [low, gzip, , high] = [...xs].sort((a, b) => a - b);
    expect((gzip - low) / (high - low)).toBeCloseTo(0.554, 2);
  });
});
