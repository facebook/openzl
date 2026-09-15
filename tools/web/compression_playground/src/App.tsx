// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Flex} from '@chakra-ui/react';
import {ToolHeader} from '@openzl/web-common';
import ResultsPanel from './components/ResultsPanel.tsx';
import SetupColumn from './components/SetupColumn.tsx';
import logoUrl from '/OpenZL_logo.png?url';

export default function App() {
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
          <SetupColumn />
          <ResultsPanel />
        </Flex>
      </Flex>
    </Flex>
  );
}
