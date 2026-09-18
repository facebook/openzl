// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {describe, it, expect, afterEach} from 'vitest';
import {render, screen, cleanup, fireEvent, within, type RenderOptions} from '@testing-library/react';
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
