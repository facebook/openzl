// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useCallback, useEffect, useState} from 'react';
import {Box, Flex} from '@chakra-ui/react';
import {ToolHeader} from '@openzl/web-common';
import ResultsPanel from './components/ResultsPanel.tsx';
import SetupColumn from './components/SetupColumn.tsx';
import UnsupportedBrowser from './components/UnsupportedBrowser.tsx';
import {toCompressorConfig, type CompressorConfig, type RunConfig, type RunState} from './benchmarkTypes.ts';
import {ITERATIONS_DEFAULT} from './compressors.ts';
import {desktopBridge, type OpenZLDesktopBridge} from './desktop.ts';
import {detectEngineSupport, engineBlocker, isolationSettled} from './engineSupport.ts';
import {runBenchmark} from './runBenchmark.ts';
import {runDesktopBenchmark} from './runDesktopBenchmark.ts';
import {makeSample} from './sample.ts';
import {useCompressorRows} from './useCompressorRows.ts';
import logoUrl from '/OpenZL_logo.png?url';

/**
 * The Figma frame the two columns sit in (node 151:2), with its own padding.
 * These are load-bearing rather than cosmetic: the setup column is a fixed
 * 520px, so everything narrower comes off the results column, and below about
 * 1400px the measurements table no longer fits beside it.
 */
const CONTENT_MAX_WIDTH = '1590px';
const CONTENT_PADDING_X = '64px';

/**
 * The blocked screen is a few sentences, so it does not use the full content
 * width. At 1590px its text would run to well over 1400px a line, far past a
 * comfortable measure, and the card would sit above most of a viewport of empty
 * page.
 */
const MESSAGE_MAX_WIDTH = '640px';

/**
 * Without this, a file released anywhere but the drop zone makes the browser
 * navigate to it and discard the run setup. The drop zone has already called
 * preventDefault by the time the event reaches the window, so checking for that
 * leaves its own `copy` cursor intact.
 */
function useBlockStrayFileDrops() {
  useEffect(() => {
    const blockDefault = (event: DragEvent) => {
      if (event.defaultPrevented) {
        return;
      }
      if (event.dataTransfer !== null) {
        event.dataTransfer.dropEffect = 'none';
      }
      event.preventDefault();
    };
    window.addEventListener('dragover', blockDefault);
    window.addEventListener('drop', blockDefault);
    return () => {
      window.removeEventListener('dragover', blockDefault);
      window.removeEventListener('drop', blockDefault);
    };
  }, []);
}

/** "row 3", "rows 2 and 3", "rows 1, 2 and 3", numbered as step 2 numbers them. */
function describeRows(positions: readonly number[]): string {
  if (positions.length === 1) {
    return `row ${String(positions[0])}`;
  }
  return `rows ${positions.slice(0, -1).join(', ')} and ${String(positions[positions.length - 1])}`;
}

/**
 * What stands in the way of the compressors running at all. A zstd or gzip row
 * with no levels would drop out of the run with nothing on the page to say so,
 * so Run waits until it has one.
 */
function configBlockersFor(compressors: readonly CompressorConfig[]): string[] {
  const emptyRows = compressors.flatMap((config, index) => (config.levels.length === 0 ? [index + 1] : []));
  return emptyRows.length > 0 ? [`Pick at least one level for ${describeRows(emptyRows)}.`] : [];
}

/** Why Run is unavailable, if it is, in the order to fix it. */
function runBlockersFor(file: File | null, compressors: readonly CompressorConfig[]): string[] {
  return [
    ...(file === null ? ['Choose a file in step 1 to run the benchmark.'] : []),
    ...configBlockersFor(compressors),
  ];
}

export default function App() {
  // The desktop app measures with `zli` instead of the WASM worker. Both feed
  // the same `RunState`, so nothing past `onRun` knows which one ran.
  const bridge = desktopBridge();
  const desktop = bridge !== undefined;
  // Lazy initializer rather than a call in the body: nothing detection reports
  // can change while the page is open, so it should run once per mount rather
  // than once per render.
  const [support] = useState(detectEngineSupport);
  // None of the engine gates apply to the desktop app, which never loads the
  // module. They would fail there anyway: app:// cannot host the isolation
  // worker, so `threads` always reads false.
  const blocker = desktop ? null : engineBlocker(support);
  const [settled, setSettled] = useState(false);
  useEffect(() => {
    void isolationSettled().then(() => {
      setSettled(true);
    });
  }, []);
  // On a first visit the page is not isolated until the isolation worker
  // reloads it, moments later. Showing `no-threads` before that would flash
  // the blocked screen at every new visitor, and the playground cannot load
  // either, so the main area stays empty until the worker has decided.
  const awaitingIsolation = blocker === 'no-threads' && !settled;

  return (
    <Flex direction="column" minH="100vh" bg="pg.pageBg">
      <ToolHeader
        title="Compression Playground"
        logoSrc={logoUrl}
        {...(desktop
          ? {
              // The page is served from app://, where the header's relative
              // links point nowhere.
              homeHref: 'https://openzl.org/',
              navigation: [
                {label: 'HOME', href: 'https://openzl.org/'},
                {label: 'API REFERENCE', href: 'https://openzl.org/api/c/compressor/'},
                {label: 'GETTING STARTED', href: 'https://openzl.org/getting-started/quick-start/'},
              ],
            }
          : {})}
      />
      {/* The header stays up on the unsupported path. It is the site
          navigation, and it is the only way off this screen. */}
      <Flex as="main" flex="1" direction="column" align="center" bg="pg.pageBg">
        {awaitingIsolation ? null : blocker === null ? (
          <Playground bridge={bridge} />
        ) : (
          <Box width="100%" maxW={MESSAGE_MAX_WIDTH} px="32px" pt={{base: '32px', md: '96px'}} pb="40px">
            <UnsupportedBrowser blocker={blocker} />
          </Box>
        )}
      </Flex>
    </Flex>
  );
}

/** Owns the run setup, so none of it exists on the blocked path, where nothing could use it. */
function Playground({bridge}: {bridge: OpenZLDesktopBridge | undefined}) {
  const [file, setFile] = useState<File | null>(null);
  const desktop = bridge !== undefined;
  const compressors = useCompressorRows({desktop});
  const [iterations, setIterations] = useState(ITERATIONS_DEFAULT);
  const [runState, setRunState] = useState<RunState>({status: 'idle'});
  const onRun = useCallback(
    (config: RunConfig) => {
      if (bridge === undefined) {
        runBenchmark(config, setRunState);
      } else {
        runDesktopBenchmark(config, bridge, setRunState);
      }
    },
    [bridge],
  );
  const compressorConfigs = compressors.rows.map(toCompressorConfig);
  const configBlockers = configBlockersFor(compressorConfigs);
  const runBlockers = runBlockersFor(file, compressorConfigs);
  const runConfig: RunConfig | null =
    file === null || runBlockers.length > 0
      ? null
      : {
          input: file,
          compressors: compressorConfigs,
          iterations,
        };
  // The sample stands in for the file, so it runs even with none chosen -- but
  // not with a row Run would refuse.
  const onTrySample =
    bridge === undefined || configBlockers.length > 0
      ? null
      : () => {
          const sample = makeSample();
          setFile(sample);
          onRun({input: sample, compressors: compressorConfigs, iterations});
        };
  useBlockStrayFileDrops();

  return (
    <Flex
      align="flex-start"
      gap="24px"
      width="100%"
      maxW={CONTENT_MAX_WIDTH}
      px={CONTENT_PADDING_X}
      pt="24px"
      pb="40px"
      direction={{base: 'column', lg: 'row'}}>
      <SetupColumn
        file={file}
        onFileChange={setFile}
        compressors={compressors}
        iterations={iterations}
        onIterationsChange={setIterations}
        runConfig={runConfig}
        runState={runState}
        onRun={onRun}
        onTrySample={onTrySample}
        runBlockers={runBlockers}
        desktop={desktop}
      />
      <ResultsPanel runState={runState} desktop={desktop} />
    </Flex>
  );
}
