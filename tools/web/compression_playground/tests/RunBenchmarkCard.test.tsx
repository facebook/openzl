// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {describe, it, expect, afterEach} from 'vitest';
import {render, screen, cleanup, type RenderOptions} from '@testing-library/react';
import {ChakraProvider} from '@chakra-ui/react';
import React from 'react';
import RunBenchmarkCard from '../src/components/RunBenchmarkCard.tsx';
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

  it('disables both actions until the run seam lands', () => {
    renderWithPlaygroundTheme(<RunBenchmarkCard />);

    // Disabled rather than wired to a no-op, so the buttons do not advertise
    // an action they cannot perform yet.
    expect(screen.getByRole('button', {name: 'Run benchmark'})).toBeDisabled();
    expect(screen.getByRole('button', {name: 'Try a 5 MB sample'})).toBeDisabled();
  });
});
