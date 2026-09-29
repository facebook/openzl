// Copyright (c) Meta Platforms, Inc. and affiliates.
// @vitest-environment jsdom

import '@testing-library/jest-dom/vitest';
import {describe, it, expect, vi, afterEach} from 'vitest';
import {screen, cleanup} from '@testing-library/react';
import userEvent from '@testing-library/user-event';

import Toolbar from '../../src/components/Toolbar';
import {renderWithChakra, makeToolbarProps} from '../utils/renderUtils';
import type {ToolbarTestProps} from '../utils/renderUtils';

const {settingsPanelSpy} = vi.hoisted(() => ({settingsPanelSpy: vi.fn()}));

vi.mock('../../src/components/Settings', () => ({
  SettingsPanel: (props: unknown) => {
    settingsPanelSpy(props);
    return <button aria-label="Settings">Settings</button>;
  },
}));

const renderToolbar = (overrides: Partial<ToolbarTestProps> = {}) => {
  const props = makeToolbarProps(overrides);
  return {
    ...renderWithChakra(<Toolbar {...props} />),
    props,
  };
};

describe('Toolbar', () => {
  // Unmount the rendered tree after each test so the DOM doesn't accumulate
  // across tests (which would make queries match duplicate elements).
  afterEach(() => {
    cleanup();
  });

  it('upload button fires onUploadCborFile', async () => {
    const {props} = renderToolbar();
    const user = userEvent.setup();
    const uploadButton = screen.getByText('UPLOAD CBOR FILE');
    await user.click(uploadButton);
    expect(props.onUploadCborFile).toHaveBeenCalledTimes(1);
  });

  it('forwards settings props to SettingsPanel', () => {
    const {props} = renderToolbar({
      isTrackpadMode: true,
      isKeyboardMode: false,
    });
    expect(screen.getByLabelText('Settings')).toBeInTheDocument();
    // Verify Toolbar actually forwards each settings prop to SettingsPanel.
    expect(settingsPanelSpy).toHaveBeenCalledWith(
      expect.objectContaining({
        isTrackpadMode: true,
        isKeyboardMode: false,
        onToggleTrackpadMode: props.onToggleTrackpadMode,
        onToggleKeyboardNav: props.onToggleKeyboardNav,
      }),
    );
  });

  it('renders nav links with correct hrefs', () => {
    renderToolbar();
    // Verify that the nav links are rendered with the correct hrefs
    expect(screen.getByText('HOME').closest('a')).toHaveAttribute('href', '/#');
    expect(screen.getByText('API REFERENCE').closest('a')).toHaveAttribute('href', '/api/c/compressor/');
    expect(screen.getByText('GETTING STARTED').closest('a')).toHaveAttribute('href', '/getting-started/quick-start/');
  });
});
