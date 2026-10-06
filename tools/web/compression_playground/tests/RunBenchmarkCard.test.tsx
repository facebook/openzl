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

  it('disables actions without their controlled inputs and handlers', () => {
    renderWithPlaygroundTheme(
      <RunBenchmarkCard runConfig={null} runState={idleState} onRun={null} onTrySample={null} runBlockers={[]} />,
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
      <RunBenchmarkCard
        runConfig={config}
        runState={idleState}
        onRun={onRun}
        onTrySample={onTrySample}
        runBlockers={[]}
      />,
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
      <RunBenchmarkCard
        runConfig={config}
        runState={{status: 'loading'}}
        onRun={vi.fn()}
        onTrySample={vi.fn()}
        runBlockers={[]}
      />,
    );

    expect(screen.getByRole('button', {name: 'Run benchmark'})).toBeDisabled();
    expect(screen.getByRole('button', {name: 'Try a 5 MB sample'})).toBeDisabled();
  });

  it('says what to fix while Run is unavailable, on focus and to a screen reader', async () => {
    const onRun = vi.fn();
    renderWithPlaygroundTheme(
      <RunBenchmarkCard
        runConfig={null}
        runState={idleState}
        onRun={onRun}
        onTrySample={null}
        runBlockers={['Choose a file in step 1 to run the benchmark.', 'Pick at least one level for row 3.']}
      />,
    );
    const run = screen.getByRole('button', {name: 'Run benchmark'});
    const reason = 'Choose a file in step 1 to run the benchmark. Pick at least one level for row 3.';

    // `aria-disabled` rather than `disabled`, which would take away the hover
    // and focus the tooltip opens on. It still refuses the click.
    expect(run).toHaveAttribute('aria-disabled', 'true');
    expect(run).not.toBeDisabled();
    fireEvent.click(run);
    expect(onRun).not.toHaveBeenCalled();

    expect(run).toHaveAccessibleDescription(reason);
    fireEvent.focus(run);
    expect(await screen.findByRole('tooltip')).toHaveTextContent(reason);
  });

  it('says nothing about blockers while a run is in flight', () => {
    // The status line already says what is happening, and nothing needs fixing.
    renderWithPlaygroundTheme(
      <RunBenchmarkCard
        runConfig={null}
        runState={{status: 'loading'}}
        onRun={vi.fn()}
        onTrySample={null}
        runBlockers={['Pick at least one level for row 3.']}
      />,
    );

    expect(screen.queryByText('Pick at least one level for row 3.')).not.toBeInTheDocument();
    expect(screen.getByRole('button', {name: 'Run benchmark'})).not.toHaveAttribute('aria-describedby');
  });
});
