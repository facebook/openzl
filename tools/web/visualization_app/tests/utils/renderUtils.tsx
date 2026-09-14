// Copyright (c) Meta Platforms, Inc. and affiliates.

import React from 'react';
import {render, type RenderOptions} from '@testing-library/react';
import {ChakraProvider, defaultSystem} from '@chakra-ui/react';
import {vi} from 'vitest';

/**
 * Render a component tree wrapped in ChakraProvider. Required for any Chakra UI
 * component (Settings, Toolbar, App toaster, etc.).
 */
export function renderWithChakra(ui: React.ReactElement, options?: Omit<RenderOptions, 'wrapper'>) {
  return render(ui, {
    wrapper: ({children}) => <ChakraProvider value={defaultSystem}>{children}</ChakraProvider>,
    ...options,
  });
}

export interface ToolbarTestProps {
  onUploadCborFile: () => void;
  onToggleTrackpadMode: () => void;
  onToggleKeyboardNav: () => void;
  isTrackpadMode: boolean;
  isKeyboardMode: boolean;
}

/** Default Toolbar props (vi.fn callbacks) with per-test overrides. */
export function makeToolbarProps(overrides: Partial<ToolbarTestProps> = {}): ToolbarTestProps {
  return {
    onUploadCborFile: vi.fn(),
    onToggleTrackpadMode: vi.fn(),
    onToggleKeyboardNav: vi.fn(),
    isTrackpadMode: false,
    isKeyboardMode: true,
    ...overrides,
  };
}
