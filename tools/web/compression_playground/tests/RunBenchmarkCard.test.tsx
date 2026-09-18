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

    expect(screen.getByRole('button', {name: 'Run benchmark'})).toBeDisabled();
    expect(screen.getByRole('button', {name: 'Try a 5 MB sample'})).toBeDisabled();
  });
});
