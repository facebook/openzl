// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment jsdom
import '@testing-library/jest-dom/vitest';
import {describe, it, expect, afterEach} from 'vitest';
import {fireEvent, render, screen, cleanup, within, waitFor, type RenderOptions} from '@testing-library/react';
import {ChakraProvider} from '@chakra-ui/react';
import React from 'react';
import ConfigureRunCard from '../src/components/ConfigureRunCard.tsx';
import {
  OPENZL_PROFILES,
  OPENZL_PROFILE_DESCRIPTIONS,
  createCompressorRow,
  tickLabelLeft,
} from '../src/compressors.ts';
import {playgroundSystem} from '../src/theme.ts';

function renderWithPlaygroundTheme(ui: React.ReactElement, options?: Omit<RenderOptions, 'wrapper'>) {
  return render(ui, {
    wrapper: ({children}) => <ChakraProvider value={playgroundSystem}>{children}</ChakraProvider>,
    ...options,
  });
}

function optionValues(select: HTMLElement): string[] {
  return within(select)
    .getAllByRole('option')
    .map((option) => (option as HTMLOptionElement).value);
}

describe('ConfigureRunCard', () => {
  afterEach(() => {
    cleanup();
  });

  it('renders the default OpenZL, zstd and gzip rows', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    expect(screen.getByText('COMPRESSOR')).toBeInTheDocument();
    expect(screen.getByText('LEVEL / PROFILE')).toBeInTheDocument();

    expect(screen.getByRole('combobox', {name: 'Compressor for row 1'})).toHaveValue('OpenZL');
    expect(screen.getByRole('combobox', {name: 'Level or profile for row 1'})).toHaveTextContent('serial');
    expect(screen.getByRole('combobox', {name: 'Compressor for row 2'})).toHaveValue('zstd');
    expect(screen.getByRole('combobox', {name: 'Level or profile for row 2'})).toHaveValue('5');
    expect(screen.getByRole('combobox', {name: 'Compressor for row 3'})).toHaveValue('gzip');
    expect(screen.getByRole('combobox', {name: 'Level or profile for row 3'})).toHaveValue('5');
  });

  it('keeps the dropdown arrow inside the positioned control', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    const trigger = screen.getByRole('combobox', {name: 'Level or profile for row 1'});
    // The chevron must sit beside the trigger, not in it: nested in the
    // trigger it positions against a distant ancestor and vanishes.
    expect(trigger.querySelector('svg')).toBeNull();
    expect(trigger.parentElement?.querySelector('svg')).not.toBeNull();
  });

  it('offers every compressor and OpenZL profile', async () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    expect(optionValues(screen.getByRole('combobox', {name: 'Compressor for row 1'}))).toEqual([
      'OpenZL',
      'zstd',
      'gzip',
    ]);

    fireEvent.click(screen.getByRole('combobox', {name: 'Level or profile for row 1'}));
    const listbox = await screen.findByRole('listbox');
    expect(
      within(listbox)
        .getAllByRole('option')
        .map((option) => option.textContent),
    ).toEqual([...OPENZL_PROFILES]);
  });

  it('explains each profile when hovered in the list', async () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    fireEvent.click(screen.getByRole('combobox', {name: 'Level or profile for row 1'}));
    const listbox = await screen.findByRole('listbox');
    const option = within(listbox).getByRole('option', {name: 'parquet'});

    fireEvent.pointerOver(within(option).getByText('parquet'));
    const tip = await screen.findByText('Parquet in the canonical format (no compression, plain encoding)');
    expect(tip).toBeInTheDocument();
    // Portalled out of the scrolling listbox, which would otherwise clip it.
    expect(tip.closest('[role="listbox"]')).toBeNull();
  });

  it('ranges levels per compressor with OpenZL defaulting to 6', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    const openZlLevel = screen.getByRole('combobox', {name: 'OpenZL level for row 1'});
    expect(optionValues(openZlLevel).map(Number)).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9]);
    expect(openZlLevel).toHaveValue('6');
    expect(within(openZlLevel).getByRole('option', {name: '6 (default)'})).toBeInTheDocument();

    expect(optionValues(screen.getByRole('combobox', {name: 'Level or profile for row 2'})).map(Number)).toEqual([
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    ]);
    expect(optionValues(screen.getByRole('combobox', {name: 'Level or profile for row 3'})).map(Number)).toEqual([
      1, 2, 3, 4, 5, 6, 7, 8, 9,
    ]);
  });

  it('describes every OpenZL profile', () => {
    for (const profile of OPENZL_PROFILES) {
      expect(OPENZL_PROFILE_DESCRIPTIONS[profile].length).toBeGreaterThan(0);
    }
  });

  it('selects a profile from the list', async () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    fireEvent.click(screen.getByRole('combobox', {name: 'Level or profile for row 1'}));
    const listbox = await screen.findByRole('listbox');
    fireEvent.click(within(listbox).getByRole('option', {name: 'parquet'}));

    await waitFor(() => {
      expect(screen.getByRole('combobox', {name: 'Level or profile for row 1'})).toHaveTextContent('parquet');
    });
  });

  it('defaults to five trained candidates', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    const candidates = screen.getByRole('combobox', {name: 'Number of trained candidates for row 1'});
    expect(optionValues(candidates).map(Number)).toEqual([1, 2, 3, 4, 5]);
    expect(candidates).toHaveValue('5');
  });

  it('swaps the second dropdown when the compressor changes', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    fireEvent.change(screen.getByRole('combobox', {name: 'Compressor for row 2'}), {
      target: {value: 'OpenZL'},
    });
    expect(screen.getByRole('combobox', {name: 'Level or profile for row 2'})).toHaveTextContent('serial');
    // Two OpenZL rows now, each with its own TRAIN checkbox.
    expect(screen.getAllByRole('checkbox', {name: 'TRAIN'})).toHaveLength(2);

    fireEvent.change(screen.getByRole('combobox', {name: 'Compressor for row 1'}), {
      target: {value: 'gzip'},
    });
    expect(optionValues(screen.getByRole('combobox', {name: 'Level or profile for row 1'})).map(Number)).toEqual([
      1, 2, 3, 4, 5, 6, 7, 8, 9,
    ]);
    expect(screen.queryByRole('combobox', {name: 'OpenZL level for row 1'})).not.toBeInTheDocument();
  });

  it('resets dependent fields when the compressor changes', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    fireEvent.change(screen.getByRole('combobox', {name: 'Level or profile for row 2'}), {
      target: {value: '19'},
    });
    expect(screen.getByRole('combobox', {name: 'Level or profile for row 2'})).toHaveValue('19');

    // 19 is not a valid gzip level, so the row falls back to the default.
    fireEvent.change(screen.getByRole('combobox', {name: 'Compressor for row 2'}), {
      target: {value: 'gzip'},
    });
    expect(screen.getByRole('combobox', {name: 'Level or profile for row 2'})).toHaveValue('5');
  });

  it('adds and removes compressor rows', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    fireEvent.click(screen.getByRole('button', {name: 'Add compressor'}));
    expect(screen.getByRole('combobox', {name: 'Compressor for row 4'})).toHaveValue('OpenZL');

    fireEvent.click(screen.getByRole('button', {name: 'Remove row 4'}));
    expect(screen.queryByRole('combobox', {name: 'Compressor for row 4'})).not.toBeInTheDocument();
  });

  it('keeps at least one row', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);

    fireEvent.click(screen.getByRole('button', {name: 'Remove row 3'}));
    fireEvent.click(screen.getByRole('button', {name: 'Remove row 2'}));
    expect(screen.getByRole('button', {name: 'Remove row 1'})).toBeDisabled();
  });

  it('collapses the OpenZL options section', () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);
    const toggle = screen.getByRole('button', {name: 'OpenZL options for row 1'});

    expect(toggle).toHaveAttribute('aria-expanded', 'true');
    fireEvent.click(toggle);
    expect(toggle).toHaveAttribute('aria-expanded', 'false');
    expect(screen.queryByRole('combobox', {name: 'OpenZL level for row 1'})).not.toBeInTheDocument();

    fireEvent.click(toggle);
    expect(screen.getByRole('combobox', {name: 'OpenZL level for row 1'})).toBeInTheDocument();
  });

  it('disables the candidate count while training is off', async () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);
    const train = screen.getByRole('checkbox', {name: 'TRAIN'});
    const candidates = screen.getByRole('combobox', {name: 'Number of trained candidates for row 1'});

    expect(train).toBeChecked();
    expect(candidates).toBeEnabled();

    fireEvent.click(train);

    // The state machine notifies parents asynchronously, so wait for the
    // consequence instead of asserting synchronously.
    await waitFor(() => {
      expect(train.closest('label')).toHaveAttribute('data-state', 'unchecked');
      expect(candidates).toBeDisabled();
    });
  });

  it('only arms training on OpenZL rows', () => {
    // TRAIN is not rendered for zstd or gzip, so a stale flag would reach the
    // run seam unseen.
    expect(createCompressorRow(1, 'OpenZL').train).toBe(true);
    expect(createCompressorRow(2, 'zstd').train).toBe(false);
    expect(createCompressorRow(3, 'gzip').train).toBe(false);
  });

  it('locks training off for the pytorch profile', async () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);
    expect(screen.getByRole('checkbox', {name: 'TRAIN'})).toBeChecked();

    fireEvent.click(screen.getByRole('combobox', {name: 'Level or profile for row 1'}));
    fireEvent.click(await screen.findByRole('option', {name: /pytorch/}));

    await waitFor(() => {
      const train = screen.getByRole('checkbox', {name: 'TRAIN'});
      expect(train).toBeDisabled();
      expect(train.closest('label')).toHaveAttribute('data-state', 'unchecked');
    });
    expect(screen.getByText('Training is not supported for the pytorch profile')).toBeInTheDocument();
  });

  it('pins tick labels to the dot travel with the thumb inset', () => {
    // Endpoints sit one half-thumb in from each edge; midpoints are
    // proportional to their value, matching the contain-alignment travel.
    expect(tickLabelLeft(1)).toBe('calc(8px + 0 * (100% - 16px))');
    expect(tickLabelLeft(5)).toBe('calc(8px + 0.2857 * (100% - 16px))');
    expect(tickLabelLeft(10)).toBe('calc(8px + 0.6429 * (100% - 16px))');
    expect(tickLabelLeft(15)).toBe('calc(8px + 1 * (100% - 16px))');
  });

  it('adjusts the iteration count with the slider', async () => {
    renderWithPlaygroundTheme(<ConfigureRunCard />);
    // The thumb stays visibility-hidden under jsdom, where the size
    // measurement that unhides it never completes, so it can only be queried
    // unnamed; the label association is asserted explicitly instead.
    const slider = screen.getByRole('slider', {hidden: true});
    const label = screen.getByText('Iterations');
    expect(slider).toHaveAttribute('aria-labelledby', label.getAttribute('id'));
    // The track collapses when composed outside the control, so pin the
    // structure, not just the behavior.
    const inControl = within(screen.getByTestId('iterations-control'));
    expect(inControl.getByRole('slider', {hidden: true})).toBe(slider);

    expect(slider).toHaveAttribute('aria-valuenow', '5');
    expect(screen.getByTestId('iterations-value')).toHaveTextContent('5');
    const ticks = within(screen.getByTestId('iteration-ticks'));
    for (const tick of ['1', '5', '10', '15']) {
      const label = ticks.getByText(tick);
      expect(label).toBeInTheDocument();
      // Centered under the dot's position for that value, not spread evenly.
      expect(label).toHaveStyle({position: 'absolute', transform: 'translateX(-50%)'});
    }

    // Keyboard input only reaches the slider once the thumb has focus, the
    // same Tab-then-arrows flow a keyboard user follows.
    fireEvent.focus(slider);
    fireEvent.keyDown(slider, {key: 'ArrowRight'});
    await waitFor(() => {
      expect(slider).toHaveAttribute('aria-valuenow', '6');
      expect(screen.getByTestId('iterations-value')).toHaveTextContent('6');
    });
  });
});
