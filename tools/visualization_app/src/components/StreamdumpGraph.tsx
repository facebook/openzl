// Copyright (c) Meta Platforms, Inc. and affiliates.

import '@xyflow/react/dist/style.css';
import type {NullableStreamdump} from '../interfaces/NullableStreamdump';
import '../styles/streamdumpGraph.css';
import {useStreamdumpGraphController} from '../graphVisualization/controllers/StreamdumpGraphController';
import {StreamdumpGraphView} from '../graphVisualization/views/StreamdumpGraphView';
import {ReactFlowProvider} from '@xyflow/react';
import {Box, Code, CodeBlock, Float, IconButton, Text, Tabs, useTabs, Heading, Span} from '@chakra-ui/react';

const cliInvocation = 'zli compress --trace /tmp/streamdump.cbor -p serial -o /dev/null myfile.txt';
const files = [
  {
    language: 'C++',
    title: 'C++',
    code: `
#include "openzl/cpp/CCtx.hpp"

CCtx myCCtx;
myCCtx.writeTraces(true);
myCCtx.compress(/* input */);
std::string trace = myCCtx.getLatestTrace();
std::ofstream out{"/home/user/trace.cbor"};
out << trace;
out.close();
`,
  },
  {
    language: 'C',
    title: 'C',
    code: `
// this feature is only available in C++ for now!
`,
  },
];

function NoDataHelper() {
  const tabs = useTabs({
    defaultValue: 'C++',
  });

  const activeTab = files.find((file) => file.language === tabs.value) || files[0];

  const otherTabs = files.filter((file) => file.language !== tabs.value);

  return (
    <Box className="no-data-helper">
      <Heading className="no-data-helper-text" size="3xl">
        Usage
      </Heading>
      <Heading className="no-data-helper-text" size="2xl">
        0. Enable introspection
      </Heading>
      <Text className="no-data-helper-text">
        This is on by default for CMake builds.{' '}
        <b>You do not need to worry about this step if you don't see any errors!</b> If you've disabled introspection in
        your CMake build, you can re-enable it by adding the following flag to your CMake configure command:{' '}
        <Code>{`-DOPENZL_ALLOW_INTROSPECTION=ON`}</Code>
      </Text>
      <br />

      <Heading className="no-data-helper-text" size="2xl">
        1. Gather a trace
      </Heading>
      <Heading className="no-data-helper-text" size="lg">
        Using the CLI
      </Heading>
      <Text className="no-data-helper-text">
        Specify the 'trace' option when compressing, and provide a .cbor path to write the trace to. For example:
      </Text>
      <CodeBlock.Root code={cliInvocation} language={'bash'} display="inline-flex">
        <CodeBlock.Content>
          <Float placement="middle-end" offsetX="6" zIndex="1">
            <CodeBlock.CopyTrigger asChild>
              <IconButton variant="ghost" size="2xs">
                <CodeBlock.CopyIndicator />
              </IconButton>
            </CodeBlock.CopyTrigger>
          </Float>
          <CodeBlock.Code pe="14">
            <Span color="fg.muted" ms="4" userSelect="none">
              $
            </Span>
            <CodeBlock.CodeText display="inline-block" />
          </CodeBlock.Code>
        </CodeBlock.Content>
      </CodeBlock.Root>
      <br />
      <br />

      <Heading className="no-data-helper-text" size="lg">
        Using the C++ API
      </Heading>
      <Text className="no-data-helper-text">Enable tracing in the CCtx</Text>
      <Tabs.RootProvider value={tabs} size="sm" variant="line">
        <CodeBlock.Root code={activeTab.code} language={activeTab.language}>
          <CodeBlock.Header borderBottomWidth="1px">
            <Tabs.List w="full" border="0" ms="-1">
              {files.map((file) => (
                <Tabs.Trigger colorPalette="teal" key={file.language} value={file.language} textStyle="xs">
                  {file.title}
                </Tabs.Trigger>
              ))}
            </Tabs.List>
            <CodeBlock.CopyTrigger asChild>
              <IconButton variant="ghost" size="2xs">
                <CodeBlock.CopyIndicator />
              </IconButton>
            </CodeBlock.CopyTrigger>
          </CodeBlock.Header>
          <CodeBlock.Content>
            {otherTabs.map((file) => (
              <Tabs.Content key={file.language} value={file.language} />
            ))}
            <Tabs.Content pt="1" value={activeTab.language}>
              <CodeBlock.Code>
                <CodeBlock.CodeText />
              </CodeBlock.Code>
            </Tabs.Content>
          </CodeBlock.Content>
        </CodeBlock.Root>
      </Tabs.RootProvider>
      <br />

      <Heading className="no-data-helper-text" size="2xl">
        2. Upload the trace
      </Heading>
      <Text className="no-data-helper-text">
        Navigate to this website (hooray! you've already done it!) and upload the CBOR file.
      </Text>
      <Text className="no-data-helper-text" fontWeight="bold">
        That's it!
      </Text>
    </Box>
  );
}

// Create a new component to use the hooks inside the provider
function StreamdumpGraphContent({data}: NullableStreamdump) {
  const {nodes, edges, onNodesChange, onEdgesChange, handleAllStandardGraphsCollapse, areStandardGraphsCollapsed} =
    useStreamdumpGraphController({data});

  if (!data) {
    return <NoDataHelper />;
  }

  return (
    <StreamdumpGraphView
      nodes={nodes}
      edges={edges}
      onNodesChange={onNodesChange}
      onEdgesChange={onEdgesChange}
      handleAllStandardGraphsCollapse={handleAllStandardGraphsCollapse}
      areStandardGraphsCollapsed={areStandardGraphsCollapsed}
    />
  );
}

export function StreamdumpGraph({data}: NullableStreamdump) {
  // Wrap everything in ReactFlowProvider
  return (
    <ReactFlowProvider>
      <StreamdumpGraphContent data={data} />
    </ReactFlowProvider>
  );
}
