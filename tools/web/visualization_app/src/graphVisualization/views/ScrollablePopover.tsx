// Copyright (c) Meta Platforms, Inc. and affiliates.

import {ScrollArea} from '@chakra-ui/react';
import type {PropsWithChildren} from 'react';

interface ScrollablePopoverProps {
  className: string;
  maxHeight: string;
}

export const ScrollablePopover = (props: PropsWithChildren<ScrollablePopoverProps>) => {
  return (
    <ScrollArea.Root overflow="visible">
      <ScrollArea.Viewport>
        <div className={props.className} style={{maxHeight: props.maxHeight}}>
          {props.children}
          <ScrollArea.Scrollbar bg="transparent">
            <ScrollArea.Thumb />
          </ScrollArea.Scrollbar>
        </div>
      </ScrollArea.Viewport>
    </ScrollArea.Root>
  );
};
