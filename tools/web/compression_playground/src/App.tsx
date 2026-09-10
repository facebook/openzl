// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useEffect, useState} from 'react';
import {Flex} from '@chakra-ui/react';
import {ToolHeader} from '@openzl/web-common';
import ResultsPanel from './components/ResultsPanel.tsx';
import SetupColumn from './components/SetupColumn.tsx';
import logoUrl from '/OpenZL_logo.png?url';

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
  // The file lives here rather than in UploadCard because the run seam needs it:
  // step 3 gates its button on having one, and it becomes RunConfig.input.
  const [file, setFile] = useState<File | null>(null);
  useBlockStrayFileDrops();

  return (
    <Flex direction="column" minH="100vh" bg="pg.pageBg">
      <ToolHeader title="Compression Playground" logoSrc={logoUrl} />
      <Flex as="main" flex="1" justify="center" bg="pg.pageBg">
        <Flex
          align="flex-start"
          gap="24px"
          width="100%"
          maxW="1223px"
          px="32px"
          pt="24px"
          pb="40px"
          direction={{base: 'column', lg: 'row'}}>
          <SetupColumn file={file} onFileChange={setFile} />
          <ResultsPanel />
        </Flex>
      </Flex>
    </Flex>
  );
}
