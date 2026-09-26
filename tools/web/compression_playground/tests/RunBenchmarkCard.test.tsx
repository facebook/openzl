// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {describe, it, expect, afterEach, vi} from 'vitest';
import {render, screen, cleanup, fireEvent, type RenderOptions} from '@testing-library/react';
import {ChakraProvider} from '@chakra-ui/react';
import React from 'react';
import RunBenchmarkCard from '../src/components/RunBenchmarkCard.tsx';
import type {RunConfig, RunState} from '../src/benchmarkTypes.ts';
import {playgroundSystem} from '../src/theme.ts';

function renderWithPlaygroundTheme(ui: React.ReactElement, options?: Omit<RenderOptions, 'wrapper'>) {
  return render(ui, {
    wrapper: ({children}) => <ChakraProvider value={playgroundSystem}>{children}</ChakraProvider>,
    ...options,
  });
}

describe('RunBenchmarkCard', () => {
  afterEach(() => {
    cleanup();
  });

  const idleState: RunState = {status: 'idle'};

  function running(over: Partial<Extract<RunState, {status: 'running'}>> = {}): RunState {
    return {status: 'running', completedJobs: 0, totalJobs: 13, results: [], failures: [], step: null, ...over};
  }

  function barPercent(): number {
    return Number(screen.getByRole('progressbar').getAttribute('aria-valuenow'));
  }

  describe('while it runs', () => {
    it('shows no progress until a run starts', () => {
      renderWithPlaygroundTheme(
        <RunBenchmarkCard runConfig={null} runState={idleState} onRun={null} onTrySample={null} />,
      );
      expect(screen.queryByRole('progressbar')).not.toBeInTheDocument();
      expect(screen.getByRole('button', {name: /run benchmark/i})).toBeInTheDocument();
    });

    it('turns the run button into a running one', () => {
      renderWithPlaygroundTheme(
        <RunBenchmarkCard runConfig={null} runState={running()} onRun={null} onTrySample={null} />,
      );
      expect(screen.getByRole('button', {name: /running/i})).toBeDisabled();
      expect(screen.queryByRole('button', {name: /run benchmark/i})).not.toBeInTheDocument();
    });

    it('counts jobs from one, because the first is the one in flight', () => {
      renderWithPlaygroundTheme(
        <RunBenchmarkCard runConfig={null} runState={running()} onRun={null} onTrySample={null} />,
      );
      expect(screen.getByText(/Running 1 out of 13/)).toBeInTheDocument();
      expect(barPercent()).toBe(0);
    });

    it('advances the bar within a job that reports its own progress', () => {
      // Training is one job that runs for minutes. Without this the bar would
      // sit on the same number for most of a trained run.
      const {rerender} = renderWithPlaygroundTheme(
        <RunBenchmarkCard runConfig={null} runState={running({step: 0.5})} onRun={null} onTrySample={null} />,
      );
      // Half of the first job of thirteen.
      expect(barPercent()).toBe(4);
      // Only the bar moves: the trainer's own text is internal jargon and the
      // label stays the job counter.
      expect(screen.getByText('Running 1 out of 13…')).toBeInTheDocument();

      rerender(
        <ChakraProvider value={playgroundSystem}>
          <RunBenchmarkCard
            runConfig={null}
            runState={running({completedJobs: 6, step: 1})}
            onRun={null}
            onTrySample={null}
          />
        </ChakraProvider>,
      );
      expect(barPercent()).toBe(54);
    });

    it('never runs the bar past the end of the run', () => {
      renderWithPlaygroundTheme(
        <RunBenchmarkCard
          runConfig={null}
          runState={running({completedJobs: 13, step: 1})}
          onRun={null}
          onTrySample={null}
        />,
      );
      expect(barPercent()).toBe(100);
    });

    it('does not count past the last job', () => {
      // The last result and `finished` are two messages, so there is a frame
      // where every job is counted and the run has not ended yet.
      renderWithPlaygroundTheme(
        <RunBenchmarkCard runConfig={null} runState={running({completedJobs: 13})} onRun={null} onTrySample={null} />,
      );
      expect(screen.getByText(/Running 13 out of 13/)).toBeInTheDocument();
    });

    it('reports what finished, and drops the progress bar', () => {
      renderWithPlaygroundTheme(
        <RunBenchmarkCard
          runConfig={null}
          runState={
            {
              status: 'completed',
              results: [
                {
                  iterations: 1,
                  srcSize: 5_000_000,
                  compressedSize: 100,
                  ratio: 10,
                  compressMs: 1,
                  decompressMs: 1,
                  compressMBps: 100,
                  decompressMBps: 200,
                  candidate: null,
                  job: {
                    id: 'j',
                    rowId: 2,
                    level: 1,
                    iterations: 1,
                    compressor: 'zstd',
                  },
                },
              ],
              failures: [],
            } as RunState
          }
          onRun={null}
          onTrySample={null}
        />,
      );

      // In a live region of its own: the progress one unmounts at the same
      // moment, and a region that goes away announces nothing.
      expect(screen.getByRole('status')).toHaveTextContent('Done — 5.0 MB · 1 succeeded, 0 failed');
      expect(screen.queryByRole('progressbar')).not.toBeInTheDocument();
      expect(screen.getByRole('button', {name: /run benchmark/i})).toBeInTheDocument();
    });

    it('says what it is doing while the module loads', () => {
      renderWithPlaygroundTheme(
        <RunBenchmarkCard runConfig={null} runState={{status: 'loading'}} onRun={null} onTrySample={null} />,
      );
      expect(screen.getByText(/Loading the OpenZL module/)).toBeInTheDocument();
      expect(barPercent()).toBe(0);
    });
  });

  it('disables actions without their controlled inputs and handlers', () => {
    renderWithPlaygroundTheme(
      <RunBenchmarkCard runConfig={null} runState={idleState} onRun={null} onTrySample={null} />,
    );

    expect(screen.getByRole('button', {name: 'Run benchmark'})).toBeDisabled();
    expect(screen.getByRole('button', {name: 'Try a 5 MB sample'})).toBeDisabled();
  });

  it('passes the current configuration to the run handler', () => {
    const config: RunConfig = {
      input: new File(['data'], 'data.bin'),
      compressors: [{rowId: 1, compressor: 'OpenZL', levels: [6], profile: 'serial', training: null}],
      iterations: 5,
    };
    const onRun = vi.fn();
    const onTrySample = vi.fn();
    renderWithPlaygroundTheme(
      <RunBenchmarkCard runConfig={config} runState={idleState} onRun={onRun} onTrySample={onTrySample} />,
    );

    fireEvent.click(screen.getByRole('button', {name: 'Run benchmark'}));
    fireEvent.click(screen.getByRole('button', {name: 'Try a 5 MB sample'}));

    expect(onRun).toHaveBeenCalledWith(config);
    expect(onTrySample).toHaveBeenCalledOnce();
  });

  it('disables actions while a run is loading', () => {
    const config: RunConfig = {
      input: new File(['data'], 'data.bin'),
      compressors: [{rowId: 1, compressor: 'OpenZL', levels: [6], profile: 'serial', training: null}],
      iterations: 5,
    };
    renderWithPlaygroundTheme(
      <RunBenchmarkCard runConfig={config} runState={{status: 'loading'}} onRun={vi.fn()} onTrySample={vi.fn()} />,
    );

    // Loading is already part of the run, so the button is the running one.
    expect(screen.getByRole('button', {name: 'Running…'})).toBeDisabled();
    expect(screen.getByRole('button', {name: 'Try a 5 MB sample'})).toBeDisabled();
  });
});
