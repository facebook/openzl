// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useEffect, useState} from 'react';
import {Box, Flex} from '@chakra-ui/react';
import {Banner, ToolHeader} from '@openzl/web-common';
import ResultsPanel from './components/ResultsPanel.tsx';
import SetupColumn from './components/SetupColumn.tsx';
import {toCompressorConfig, type RunConfig, type RunState} from './benchmarkTypes.ts';
import {ITERATIONS_DEFAULT} from './compressors.ts';
import {useCompressorRows} from './useCompressorRows.ts';
import logoUrl from '/OpenZL_logo.png?url';

/** Content width of the Figma frame (node 29:4) the setup and results columns sit in. */
const CONTENT_MAX_WIDTH = '1223px';

/** The only state a run can be in until the run seam lands, hence `onRun={null}` below. */
const IDLE_RUN_STATE: RunState = {status: 'idle'};

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

export default function App() {
  const [file, setFile] = useState<File | null>(null);
  const compressors = useCompressorRows();
  const [iterations, setIterations] = useState(ITERATIONS_DEFAULT);
  const runConfig: RunConfig | null =
    file === null
      ? null
      : {
          input: file,
          compressors: compressors.rows.map(toCompressorConfig),
          iterations,
        };
  useBlockStrayFileDrops();

  return (
    <Flex direction="column" minH="100vh" bg="pg.pageBg">
      <ToolHeader title="Compression Playground" logoSrc={logoUrl} />
      <Flex as="main" flex="1" direction="column" align="center" bg="pg.pageBg">
        {/* The docs site publishes every land, so the page is reachable well
            before the run seam exists. Without this the setup UI looks like a
            finished tool that silently does nothing. */}
        <Box width="100%" maxW={CONTENT_MAX_WIDTH} px="32px" pt="24px">
          <Banner>Work in progress — benchmarking is not wired up yet</Banner>
        </Box>
        <Flex
          align="flex-start"
          gap="24px"
          width="100%"
          maxW={CONTENT_MAX_WIDTH}
          px="32px"
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
            runState={IDLE_RUN_STATE}
            onRun={null}
            onTrySample={null}
          />
          <ResultsPanel runState={IDLE_RUN_STATE} />
        </Flex>
      </Flex>
    </Flex>
  );
}
