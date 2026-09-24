// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {afterEach, describe, expect, it} from 'vitest';
import {cleanup, fireEvent, render, screen, within, type RenderOptions} from '@testing-library/react';
import {ChakraProvider} from '@chakra-ui/react';
import React from 'react';
import MeasurementsTable from '../src/components/MeasurementsTable.tsx';
import ResultsPanel from '../src/components/ResultsPanel.tsx';
import {playgroundSystem} from '../src/theme.ts';
import type {BenchmarkJob, JobResult, RunState} from '../src/benchmarkTypes.ts';

function renderWithPlaygroundTheme(ui: React.ReactElement, options?: Omit<RenderOptions, 'wrapper'>) {
  return render(ui, {
    wrapper: ({children}) => <ChakraProvider value={playgroundSystem}>{children}</ChakraProvider>,
    ...options,
  });
}

function result(job: Partial<BenchmarkJob> & Pick<BenchmarkJob, 'compressor'>, metrics: Partial<JobResult> = {}) {
  return {
    iterations: 1,
    srcSize: 1000,
    compressedSize: 100,
    ratio: 10,
    compressMs: 1,
    decompressMs: 1,
    compressMBps: 100,
    decompressMBps: 200,
    candidate: null,
    ...metrics,
    job: {
      id: `${job.compressor ?? 'x'}-${String(job.level ?? 1)}`,
      rowId: 1,
      level: 1,
      iterations: 1,
      profile: 0,
      training: null,
      ...job,
    } as BenchmarkJob,
  } satisfies JobResult;
}

function completed(results: readonly JobResult[]): RunState {
  return {status: 'completed', results, failures: []};
}

const TWO_ZSTD_LEVELS = [
  result({compressor: 'zstd', rowId: 2, level: 1}, {ratio: 3, compressedSize: 333}),
  result({compressor: 'zstd', rowId: 2, level: 9}, {ratio: 9, compressedSize: 111}),
];

afterEach(cleanup);

describe('MeasurementsTable', () => {
  it('renders nothing before a run has produced anything', () => {
    const {container} = renderWithPlaygroundTheme(<MeasurementsTable runState={{status: 'idle'}} />);
    expect(container).toBeEmptyDOMElement();
  });

  it('sizes its columns in shares of the container, not pixels', () => {
    // `table-layout: fixed` hands a column the width it asks for and lets the
    // table overflow, so pixel widths put this outside the results card. jsdom
    // does no layout, so the invariant is what gets checked, not the geometry.
    renderWithPlaygroundTheme(<MeasurementsTable runState={completed(TWO_ZSTD_LEVELS)} />);

    const widths = screen.getAllByRole('columnheader').map((th) => getComputedStyle(th).width);
    expect(widths.every((width) => width.endsWith('%'))).toBe(true);
    expect(widths.reduce((total, width) => total + Number.parseFloat(width), 0)).toBe(100);
  });

  it('summarises a group by the span of its rows', () => {
    renderWithPlaygroundTheme(<MeasurementsTable runState={completed(TWO_ZSTD_LEVELS)} />);

    const groupRow = screen.getByRole('button', {name: 'Collapse zstd, 2 levels'}).closest('tr');
    expect(groupRow).not.toBeNull();
    expect(within(groupRow as HTMLElement).getByText('2 levels')).toBeInTheDocument();
    expect(within(groupRow as HTMLElement).getByText('111 B - 333 B')).toBeInTheDocument();
    expect(within(groupRow as HTMLElement).getByText('3× - 9×')).toBeInTheDocument();
  });

  it('opens every group with the run and shuts one on demand', () => {
    renderWithPlaygroundTheme(<MeasurementsTable runState={completed(TWO_ZSTD_LEVELS)} />);
    // What the run measured is the answer; a summary standing in for rows
    // already on screen is a click between the reader and the numbers.
    expect(screen.getByText('Level 9')).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', {name: 'Collapse zstd, 2 levels'}));
    expect(screen.queryByText('Level 9')).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', {name: 'Expand zstd, 2 levels'}));
    expect(screen.getByText('Level 9')).toBeInTheDocument();
  });

  it('opens the groups again for the next run rather than keeping the last one shut', () => {
    // Row ids come back between runs, so a collapse kept across one would
    // apply to groups it was never made for.
    const {rerender} = renderWithPlaygroundTheme(<MeasurementsTable runState={completed(TWO_ZSTD_LEVELS)} />);
    fireEvent.click(screen.getByRole('button', {name: 'Collapse zstd, 2 levels'}));
    expect(screen.queryByText('Level 9')).not.toBeInTheDocument();

    rerender(<MeasurementsTable runState={{status: 'loading'}} />);
    rerender(<MeasurementsTable runState={completed(TWO_ZSTD_LEVELS)} />);

    expect(screen.getByText('Level 9')).toBeInTheDocument();
  });

  it('reorders rows within a group and flips direction on a second press', () => {
    renderWithPlaygroundTheme(<MeasurementsTable runState={completed(TWO_ZSTD_LEVELS)} />);
    const levelsInOrder = () => screen.getAllByText(/^Level \d+$/).map((node) => node.textContent);
    expect(levelsInOrder()).toEqual(['Level 1', 'Level 9']);

    fireEvent.click(screen.getByRole('button', {name: 'Sort by ratio'}));
    expect(levelsInOrder()).toEqual(['Level 9', 'Level 1']);

    fireEvent.click(screen.getByRole('button', {name: 'Sort by ratio'}));
    expect(levelsInOrder()).toEqual(['Level 1', 'Level 9']);
  });

  it('sorts by level, which no metric column stands in for', () => {
    // `LEVEL / PROFILE` holds text, so it sorts on the level behind it rather
    // than on what the cell says.
    renderWithPlaygroundTheme(<MeasurementsTable runState={completed(TWO_ZSTD_LEVELS)} />);

    fireEvent.click(screen.getByRole('button', {name: 'Sort by level / profile'}));
    expect(screen.getAllByText(/^Level \d+$/).map((node) => node.textContent)).toEqual(['Level 9', 'Level 1']);

    fireEvent.click(screen.getByRole('button', {name: 'Sort by level / profile'}));
    expect(screen.getAllByText(/^Level \d+$/).map((node) => node.textContent)).toEqual(['Level 1', 'Level 9']);
  });

  it('renders a group of one as a plain row rather than repeating its numbers', () => {
    renderWithPlaygroundTheme(
      <MeasurementsTable
        runState={completed([result({compressor: 'gzip', rowId: 3, level: 6}, {ratio: 3, compressedSize: 333})])}
      />,
    );

    expect(screen.queryByRole('button', {name: /gzip/})).not.toBeInTheDocument();
    expect(screen.getByText('gzip')).toBeInTheDocument();
    expect(screen.getByText('Level 6')).toBeInTheDocument();
    expect(screen.getAllByText('333 B')).toHaveLength(1);
  });

  it('names the ends of a trained frontier and its profile', () => {
    renderWithPlaygroundTheme(
      <MeasurementsTable
        runState={completed([
          result({compressor: 'OpenZL', rowId: 1, profile: 0}, {candidate: {index: 1, total: 3}}),
          result({compressor: 'OpenZL', rowId: 1, profile: 0}, {candidate: {index: 2, total: 3}}),
          result({compressor: 'OpenZL', rowId: 1, profile: 0}, {candidate: {index: 3, total: 3}}),
        ])}
      />,
    );

    expect(screen.getByText('max ratio')).toBeInTheDocument();
    expect(screen.getByText('balanced')).toBeInTheDocument();
    expect(screen.getByText('max speed')).toBeInTheDocument();
    expect(screen.getByText('trained · serial')).toBeInTheDocument();
    expect(screen.getAllByText('-')).toHaveLength(3);
  });
});

describe('ResultsPanel', () => {
  it('drops the empty-state card once there are measurements', () => {
    const {rerender} = renderWithPlaygroundTheme(<ResultsPanel runState={{status: 'idle'}} />);
    expect(screen.getByText('No results yet')).toBeInTheDocument();

    rerender(
      <ChakraProvider value={playgroundSystem}>
        <ResultsPanel runState={completed(TWO_ZSTD_LEVELS)} />
      </ChakraProvider>,
    );
    expect(screen.queryByText('No results yet')).not.toBeInTheDocument();
    expect(screen.getByRole('button', {name: 'Collapse zstd, 2 levels'})).toBeInTheDocument();
    expect(screen.getByText(/WebAssembly/)).toBeInTheDocument();
  });

  it('keeps the empty state when every job failed, and says which', () => {
    renderWithPlaygroundTheme(
      <ResultsPanel
        runState={{
          status: 'completed',
          results: [],
          failures: [{job: TWO_ZSTD_LEVELS[0].job, message: 'out of memory'}],
        }}
      />,
    );

    expect(screen.getByText('No results yet')).toBeInTheDocument();
    expect(screen.getByText('zstd 1: out of memory')).toBeInTheDocument();
    // The notice is about numbers on screen, and there are none.
    expect(screen.queryByText(/WebAssembly/)).not.toBeInTheDocument();
  });

  it('still says the count in the live region once the run is over', () => {
    // The pill says it on screen, but a region that empties announces nothing,
    // so a screen reader would hear no end to `Running 1 of 2…`.
    renderWithPlaygroundTheme(<ResultsPanel runState={completed(TWO_ZSTD_LEVELS)} />);

    expect(screen.getByRole('status')).toHaveTextContent('2 measured');
  });
});
