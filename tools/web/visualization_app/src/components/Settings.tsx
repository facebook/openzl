// Copyright (c) Meta Platforms, Inc. and affiliates.

import {VscGear} from 'react-icons/vsc';
import {
  Box,
  CloseButton,
  Drawer,
  Portal,
  VStack,
  HStack,
  Spacer,
  IconButton,
  SegmentGroup,
  Kbd,
  Switch,
} from '@chakra-ui/react';

function DrawerBody({
  onToggleTrackpadMode,
  onToggleKeyboardNav,
  isTrackpadMode,
  isKeyboardMode,
}: {
  onToggleTrackpadMode: () => void;
  onToggleKeyboardNav: () => void;
  isTrackpadMode: boolean;
  isKeyboardMode: boolean;
}) {
  return (
    <div className="settings-content">
      <VStack align="stretch" gap={4}>
        <Box className="legend-info-box">
          <h4 className="info-box-title">Controls</h4>
          <HStack>
            <SegmentGroup.Root
              value={isTrackpadMode ? 'Trackpad' : 'Mouse'}
              onValueChange={() => onToggleTrackpadMode()}>
              <SegmentGroup.Indicator />
              <SegmentGroup.Items items={['Mouse', 'Trackpad']} />
            </SegmentGroup.Root>
            <Spacer />
            <Kbd marginRight={2}>T</Kbd>
          </HStack>
        </Box>
        <Box className="legend-info-box">
          <h4 className="info-box-title">Navigation</h4>
          <HStack>
            <Switch.Root checked={isKeyboardMode} defaultChecked onCheckedChange={() => onToggleKeyboardNav()}>
              <Switch.HiddenInput />
              <Switch.Control />
              <Switch.Label>Toggle keyboard shortcuts</Switch.Label>
            </Switch.Root>
            <Spacer />
            <Kbd marginRight={2}>K</Kbd>
          </HStack>
        </Box>
      </VStack>
    </div>
  );
}

export function SettingsPanel({
  onToggleTrackpadMode,
  onToggleKeyboardNav,
  isTrackpadMode,
  isKeyboardMode,
}: {
  onToggleTrackpadMode: () => void;
  onToggleKeyboardNav: () => void;
  isTrackpadMode: boolean;
  isKeyboardMode: boolean;
}) {
  return (
    <Drawer.Root size="md">
      <Drawer.Trigger asChild>
        <IconButton className="toolbar-button" aria-label="Settings">
          <VscGear />
        </IconButton>
      </Drawer.Trigger>
      <Portal>
        <Drawer.Backdrop />
        <Drawer.Positioner>
          <Drawer.Content className="drawer">
            <Drawer.Header>
              <Drawer.Title className="drawer">Settings</Drawer.Title>
            </Drawer.Header>
            <Drawer.Body>
              <DrawerBody
                onToggleTrackpadMode={onToggleTrackpadMode}
                onToggleKeyboardNav={onToggleKeyboardNav}
                isTrackpadMode={isTrackpadMode}
                isKeyboardMode={isKeyboardMode}
              />
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
