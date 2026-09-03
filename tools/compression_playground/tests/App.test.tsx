// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import {describe, it, expect} from 'vitest';
import {render, screen} from '@testing-library/react';
import App from '../src/App.tsx';

describe('Compression Playground', () => {
  it('renders title and work in progress notice', () => {
    render(<App />);
    expect(screen.getByRole('main')).toBeTruthy();
    expect(
      screen.getByRole('heading', {
        level: 1,
        name: 'OpenZL Compression Playground',
      }),
    ).toBeTruthy();
    expect(screen.getByText('Work in progress...')).toBeTruthy();
  });
});
