// Copyright (c) Meta Platforms, Inc. and affiliates.
// @vitest-environment jsdom

import '@testing-library/jest-dom/vitest';
import {describe, it, expect, vi, beforeEach, afterEach, afterAll} from 'vitest';
import App from '../../src/App.tsx';
import {extractStreamdumpFromCborFile} from '../../src/utils/decodeCbor.ts';
import {renderWithChakra} from '../utils/renderUtils';
import {screen, fireEvent, waitFor, cleanup, within} from '@testing-library/react';
import userEvent from '@testing-library/user-event';

// jsdom doesn't implement ResizeObserver, which Chakra's radio group (used in
// the Settings panel) relies on. Stub it so rendering doesn't throw.
vi.stubGlobal(
  'ResizeObserver',
  class {
    observe() {}
    unobserve() {}
    disconnect() {}
  },
);

// Restore globals stubbed above so the ResizeObserver stub doesn't leak into
// other test files sharing the same Vitest worker.
afterAll(() => {
  vi.unstubAllGlobals();
});

vi.mock('../../src/utils/decodeCbor.ts', () => ({
  extractStreamdumpFromCborFile: vi.fn().mockResolvedValue({mockStreamdump: true}),
}));

vi.mock('../../src/components/StreamdumpGraph.tsx', () => ({
  StreamdumpGraph: (props: any) => (
    <div data-testid="streamdump-graph" data-has-data={!!props.data}>
      Graph
    </div>
  ),
}));

const renderApp = () => renderWithChakra(<App />);

const openSettings = (user: ReturnType<typeof userEvent.setup>) => user.click(screen.getByLabelText('Settings'));
// Scope the Close lookup to the Settings drawer dialog. A toast (with its own
// 'Close' trigger) may be present from the global T/K shortcut handler, so an
// unscoped getByLabelText('Close') could match the wrong button.
const closeSettings = (user: ReturnType<typeof userEvent.setup>) =>
  user.click(within(screen.getByRole('dialog')).getByLabelText('Close'));

describe('App', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  // Unmount the rendered tree after each test so the DOM doesn't accumulate
  // across tests (which would make queries match duplicate elements).
  afterEach(() => {
    cleanup();
  });

  it('decodes an uploaded CBOR file and passes the data to the graph', async () => {
    const {container} = renderApp();
    const fileInput = container.querySelector('input[type="file"]') as HTMLInputElement;
    expect(fileInput).toBeTruthy();

    const file = new File(['dummy'], 'test.cbor', {type: 'application/octet-stream'});
    fireEvent.change(fileInput, {target: {files: [file]}});

    await waitFor(() => {
      expect(extractStreamdumpFromCborFile).toHaveBeenCalledWith(file);
    });

    // StreamdumpGraph should receive the decoded data
    const graph = screen.getByTestId('streamdump-graph');
    await waitFor(() => {
      expect(graph.getAttribute('data-has-data')).toBe('true');
    });
  });

  it('global T shortcut toggles trackpad mode, reflected in the settings panel', async () => {
    renderApp();
    const user = userEvent.setup();

    // Toggle while focus is on the body (no input focused), then open the panel.
    await user.keyboard('{t}');
    await openSettings(user);

    await waitFor(() => {
      expect(screen.getByRole('radio', {name: 'Trackpad'})).toBeChecked();
    });

    await closeSettings(user);
    await user.keyboard('{t}');
    await openSettings(user);

    // Toggle again and verify that the change is reflected in the panel.
    await waitFor(() => {
      expect(screen.getByRole('radio', {name: 'Trackpad'})).not.toBeChecked();
    });
  });

  it('global K shortcut toggles keyboard mode, reflected in the settings panel', async () => {
    renderApp();
    const user = userEvent.setup();

    // Keyboard mode starts enabled; pressing K disables it.
    await user.keyboard('{k}');
    await openSettings(user);

    await waitFor(() => {
      expect(screen.getByLabelText('Toggle keyboard shortcuts', {selector: 'input'})).not.toBeChecked();
    });

    await closeSettings(user);
    await user.keyboard('{k}');
    await openSettings(user);

    // Toggle again and verify that the change is reflected in the panel.
    await waitFor(() => {
      expect(screen.getByLabelText('Toggle keyboard shortcuts', {selector: 'input'})).toBeChecked();
    });
  });
});
