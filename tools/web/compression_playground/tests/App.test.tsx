// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {describe, it, expect, afterEach} from 'vitest';
import {render, screen, cleanup, fireEvent, within, waitFor, type RenderOptions} from '@testing-library/react';
import {ChakraProvider} from '@chakra-ui/react';
import React from 'react';
import App from '../src/App.tsx';
import {playgroundSystem} from '../src/theme.ts';

function renderWithPlaygroundTheme(ui: React.ReactElement, options?: Omit<RenderOptions, 'wrapper'>) {
  return render(ui, {
    wrapper: ({children}) => <ChakraProvider value={playgroundSystem}>{children}</ChakraProvider>,
    ...options,
  });
}

describe('Compression Playground', () => {
  // Unmount the rendered tree after each test so the DOM doesn't accumulate
  // across tests (which would make queries match duplicate elements).
  afterEach(() => {
    cleanup();
  });

  it('renders the shared header', () => {
    renderWithPlaygroundTheme(<App />);
    expect(screen.getByRole('main')).toBeTruthy();
    expect(screen.getByRole('heading', {level: 1, name: 'Compression Playground'})).toBeInTheDocument();
    expect(screen.getByRole('link', {name: 'API REFERENCE'})).toHaveAttribute('href', '/api/c/compressor/');
    // Vite resolves the asset differently under test (through the alias in
    // vite.config.ts) than in a build (behind `base`), so assert the asset
    // rather than a literal path.
    expect(screen.getByRole('img', {name: 'OpenZL'})).toHaveAttribute(
      'src',
      expect.stringContaining('OpenZL_logo.png'),
    );
  });

  it('renders the setup steps and the data upload control', () => {
    renderWithPlaygroundTheme(<App />);
    expect(screen.getByRole('heading', {level: 2, name: 'Choose your data'})).toBeInTheDocument();
    expect(screen.getByText('Choose data to compress, files are not uploaded to a server')).toBeInTheDocument();
    expect(screen.getByText('Drag & drop a file here')).toBeInTheDocument();
    expect(screen.getByText('browse your computer')).toBeInTheDocument();
    expect(screen.getByRole('heading', {level: 2, name: 'Configure the run'})).toBeInTheDocument();
    expect(screen.getByText('Select which compressors to compare')).toBeInTheDocument();
    expect(screen.getByRole('heading', {level: 2, name: 'Run the benchmark'})).toBeInTheDocument();
    expect(screen.getByText('Measure ratio and speed for every compressor you selected')).toBeInTheDocument();
  });

  it('holds the chosen file so later steps can read it', () => {
    renderWithPlaygroundTheme(<App />);
    const file = new File(['x'.repeat(4_700_000)], 'dataset.bin', {type: 'application/octet-stream'});

    fireEvent.change(screen.getByLabelText(/browse your computer/), {target: {files: [file]}});

    // Scoped to the drop zone: the live region repeats the name and size.
    const dropzone = within(screen.getByTestId('upload-dropzone'));
    expect(dropzone.getByText('dataset.bin')).toBeInTheDocument();
    expect(dropzone.getByText(/4\.7 MB/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', {name: 'Remove dataset.bin'}));

    expect(screen.queryByText('dataset.bin')).not.toBeInTheDocument();
    expect(screen.getByText('Drag & drop a file here')).toBeInTheDocument();
  });

  it('holds Run back while a zstd or gzip row has no levels, and says why', async () => {
    // Such a row would drop out of the run with nothing on the page to say so.
    renderWithPlaygroundTheme(<App />);
    const run = screen.getByRole('button', {name: 'Run benchmark'});
    expect(run).toHaveAttribute('aria-disabled', 'true');
    expect(run).toHaveAccessibleDescription('Choose a file in step 1 to run the benchmark.');

    fireEvent.change(screen.getByLabelText(/browse your computer/), {
      target: {files: [new File(['x'], 'data.bin')]},
    });
    expect(run).not.toHaveAttribute('aria-disabled');
    expect(run).not.toHaveAttribute('aria-describedby');

    const trigger = screen.getByRole('button', {name: /, row 3$/});
    fireEvent.click(trigger);
    const levels = within(
      await waitFor(() => document.getElementById(trigger.getAttribute('aria-controls') ?? '') as HTMLElement),
    );
    fireEvent.click(levels.getByText('None'));
    await waitFor(() => {
      expect(run).toHaveAttribute('aria-disabled', 'true');
    });
    expect(run).toHaveAccessibleDescription('Pick at least one level for row 3.');

    fireEvent.click(levels.getByRole('checkbox', {name: 'Level 1'}));
    await waitFor(() => {
      expect(run).not.toHaveAttribute('aria-disabled');
    });
    expect(run).not.toHaveAttribute('aria-describedby');
  });

  it('renders the static Results panel', () => {
    renderWithPlaygroundTheme(<App />);
    expect(screen.getByRole('heading', {level: 2, name: 'Results'})).toBeInTheDocument();
    expect(screen.getByText('No results yet')).toBeInTheDocument();
    expect(screen.getByText(/Try a 5 MB sample.*to see the playground work/)).toBeInTheDocument();
    expect(screen.getByRole('heading', {level: 3, name: 'How it works'})).toBeInTheDocument();
    expect(screen.getByText('Choose your data.')).toBeInTheDocument();
    expect(screen.getByRole('heading', {level: 3, name: 'Reading the results'})).toBeInTheDocument();
    expect(screen.getByText(/What the charts say/)).toBeInTheDocument();
    expect(screen.getByText(/Train an OpenZL compressor/)).toBeInTheDocument();
    expect(screen.getByText('Get large files? Use the desktop app.')).toBeInTheDocument();
  });
});
