// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {describe, it, expect} from 'vitest';
import {render, screen} from '@testing-library/react';
import App from '../src/App.tsx';

describe('Compression Playground', () => {
  it('renders the shared header and work in progress notice', () => {
    render(<App />);
    expect(screen.getByRole('main')).toBeTruthy();
    expect(screen.getByRole('heading', {level: 1, name: 'Compression Playground'})).toBeInTheDocument();
    expect(screen.getByText('Work in progress...')).toBeInTheDocument();
    expect(screen.getByRole('link', {name: 'API REFERENCE'})).toHaveAttribute('href', '/api/c/compressor/');
    // Vite resolves the asset differently under test (through the alias in
    // vite.config.ts) than in a build (behind `base`), so assert the asset
    // rather than a literal path.
    expect(screen.getByRole('img', {name: 'OpenZL'})).toHaveAttribute(
      'src',
      expect.stringContaining('OpenZL_logo.png'),
    );
  });
});
