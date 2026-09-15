// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Flex} from '@chakra-ui/react';
import {Banner, ToolHeader} from '@openzl/web-common';
import ResultsPanel from './components/ResultsPanel.tsx';
import SetupColumn from './components/SetupColumn.tsx';
import logoUrl from '/OpenZL_logo.png?url';

/** Content width of the Figma frame (node 29:4) the setup and results columns sit in. */
const CONTENT_MAX_WIDTH = '1223px';

export default function App() {
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
          <SetupColumn />
          <ResultsPanel />
        </Flex>
      </Flex>
    </Flex>
  );
}
