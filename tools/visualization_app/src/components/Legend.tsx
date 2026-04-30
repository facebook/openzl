// Copyright (c) Meta Platforms, Inc. and affiliates.

import {VscChevronDown, VscEye, VscEyeClosed, VscFoldDown, VscFoldUp, VscQuestion} from 'react-icons/vsc';
import {Box, CloseButton, Drawer, Portal, HStack, VStack, IconButton, Table, Kbd} from '@chakra-ui/react';
import {CiCircleMore} from '../icons/TablerIcons';
import '../styles/legend.css';
import '../styles/toolbar.css';

function DrawerBody() {
  return (
    <div className="legend-content">
      <VStack align={'left'} gap={4}>
        <VStack align={'left'} gap={0}>
          <HStack gap={2}>
            <VscFoldDown className="legend-icon-svg" size={20} />
            <span>Fully show subgraph</span>
          </HStack>
          <HStack gap={2}>
            <VscFoldUp className="legend-icon-svg" size={20} />
            <span>Hide subgraph</span>
          </HStack>
          <HStack gap={2}>
            <VscChevronDown className="legend-icon-svg" size={20} />
            <span>Show one level</span>
          </HStack>
          <HStack gap={2}>
            <VscEyeClosed className="legend-icon-svg" size={20} />
            <span>Hide graph component</span>
          </HStack>
          <HStack gap={2}>
            <VscEye className="legend-icon-svg" size={20} />
            <span>Show graph component</span>
          </HStack>
          <HStack gap={2}>
            <CiCircleMore className="legend-icon-svg" size={20} />
            <span>Display LocalParams</span>
          </HStack>
        </VStack>

        {/* Graph format buttons */}
        <Box className="legend-info-box">
          <h4 className="info-box-title">Codec Node Format</h4>
          <div className="info-box-content">
            <p>CodecName (CodecNumber)</p>
            <p>CodecType | HeaderSize</p>
          </div>
        </Box>
        <Box className="legend-info-box">
          <h4 className="info-box-title">Edge Label Format</h4>
          <div className="info-box-content">
            <p> S{'<streamID>'} | StreamType</p>
            <p>CompressedSize [Share%]</p>
            <p>#Elts [EltWidth]</p>
          </div>
        </Box>
        {/* Local Parameters formatting */}
        <Box className="legend-info-box">
          <h4 className="info-box-title">Local Parameters Format</h4>
          <div className="info-box-content">
            <div className="param-example">
              <p>
                <b>Int Parameters:</b>
              </p>
              <p>(ParamId, ParamValue)</p>
            </div>
            <div className="param-example">
              <p>
                <b>Copy Parameters:</b>
              </p>
              <p>(ParamId, ParamSize, ParamData)</p>
            </div>
            <div className="param-example">
              <p>
                <b>Ref Parameters:</b>
              </p>
              <p>(ParamId)</p>
            </div>
          </div>
        </Box>
        <Box className="legend-info-box">
          <h4 className="info-box-title">Keyboard Shortcuts</h4>
          <div className="info-box-content">
            <Table.Root size="sm" variant="outline">
              <Table.Header>
                <Table.Row>
                  <Table.ColumnHeader>Shortcut</Table.ColumnHeader>
                  <Table.ColumnHeader>Action</Table.ColumnHeader>
                </Table.Row>
              </Table.Header>
              <Table.Body>
                <Table.Row>
                  <Table.Cell>
                    <Kbd>K</Kbd>
                  </Table.Cell>
                  <Table.Cell>Toggle keyboard shortcuts</Table.Cell>
                </Table.Row>
                <Table.Row>
                  <Table.Cell>
                    <Kbd>T</Kbd>
                  </Table.Cell>
                  <Table.Cell>Toggle trackpad mode</Table.Cell>
                </Table.Row>
                <Table.Row>
                  <Table.Cell>
                    <Kbd>↑</Kbd> <Kbd>↓</Kbd>
                  </Table.Cell>
                  <Table.Cell>Navigate along path</Table.Cell>
                </Table.Row>
                <Table.Row>
                  <Table.Cell>
                    <Kbd>←</Kbd> <Kbd>→</Kbd>
                  </Table.Cell>
                  <Table.Cell>Cycle siblings</Table.Cell>
                </Table.Row>
                <Table.Row>
                  <Table.Cell>
                    <Kbd>Ctrl</Kbd>+Click
                  </Table.Cell>
                  <Table.Cell>Jump to node</Table.Cell>
                </Table.Row>
                <Table.Row>
                  <Table.Cell>
                    <Kbd>Tab</Kbd>
                  </Table.Cell>
                  <Table.Cell>Navigate to siblings with next smallest stream share</Table.Cell>
                </Table.Row>
                <Table.Row>
                  <Table.Cell>
                    <Kbd>Tab</Kbd>+<Kbd>Shift</Kbd>
                  </Table.Cell>
                  <Table.Cell>Navigate to siblings with next largest stream share</Table.Cell>
                </Table.Row>
              </Table.Body>
            </Table.Root>
          </div>
        </Box>
      </VStack>
    </div>
  );
}

export function Legend() {
  return (
    <Drawer.Root size="md">
      <Drawer.Trigger asChild>
        <IconButton className="toolbar-button">
          <VscQuestion />
        </IconButton>
      </Drawer.Trigger>
      <Portal>
        <Drawer.Backdrop />
        <Drawer.Positioner>
          <Drawer.Content className="drawer">
            <Drawer.Header>
              <Drawer.Title className="drawer">Legend</Drawer.Title>
            </Drawer.Header>
            <Drawer.Body>
              <DrawerBody />
            </Drawer.Body>
            <Drawer.CloseTrigger asChild>
              <CloseButton size="sm" />
            </Drawer.CloseTrigger>
          </Drawer.Content>
        </Drawer.Positioner>
      </Portal>
    </Drawer.Root>
  );
}
