// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Handle, Position} from '@xyflow/react';
import type {InternalEdge} from '../models/InternalEdge';
import {Box} from '@chakra-ui/react/box';
import {Separator} from '@chakra-ui/react/separator';
import {ZL_Type} from '../../models/idTypes';
import {Float, Popover} from '@chakra-ui/react';
import {MdInfoOutline} from 'react-icons/md';
import type {ReactNode} from 'react';

interface EdgeViewProps {
  data: {
    internalNode: InternalEdge;
  };
}

const MAX_LINES = 4;
const BYTES_PER_LINE = 8;
const MAX_STRING_LEN = 40;

const HIGHLIGHT_COLOR = '#cfffafcb';

// Format bytes as hex dump with highlighting for struct streams
function formatSerialPreview(bytes: number[], eltWidth: number, isStruct: boolean): ReactNode {
  if (!bytes || bytes.length === 0) {
    return 'Stream Preview (0 bytes)';
  }

  const header = isStruct ? `Stream Preview (struct - eltWidth: ${eltWidth})` : 'Stream Preview (serial)';

  const totalLines = Math.ceil(bytes.length / BYTES_PER_LINE);
  const linesToShow = Math.min(MAX_LINES, totalLines);

  const lines: ReactNode[] = [];

  for (let line = 0; line < linesToShow; line++) {
    const offset = line * BYTES_PER_LINE;
    const lineBytes = Math.min(BYTES_PER_LINE, bytes.length - offset);

    const lineElements: ReactNode[] = [];

    // Offset column (no highlight)
    lineElements.push(<span key={`offset-${line}`}>{offset.toString(16).padStart(8, '0')} </span>);

    // Hex bytes - group consecutive bytes with same highlight state
    let currentHexChunk = '';
    let currentHexHighlight = false;

    for (let i = 0; i <= lineBytes; i++) {
      const byteIndex = offset + i;
      const elementIndex = Math.floor(byteIndex / eltWidth);
      const shouldHighlight = isStruct && elementIndex % 2 === 1;

      // If we've hit the end or highlight state changed, save the current chunk
      if (i === lineBytes || (i > 0 && shouldHighlight !== currentHexHighlight)) {
        if (currentHexChunk) {
          lineElements.push(
            <span key={`hex-${line}-${i}`} style={currentHexHighlight ? {backgroundColor: HIGHLIGHT_COLOR} : undefined}>
              {currentHexChunk}
            </span>,
          );
        }
        currentHexChunk = '';
        currentHexHighlight = shouldHighlight;
      }

      if (i < lineBytes) {
        currentHexChunk += bytes[byteIndex].toString(16).padStart(2, '0') + ' ';
      }
    }

    // Pad remaining (no highlight)
    if (lineBytes < BYTES_PER_LINE) {
      lineElements.push(<span key={`pad-${line}`}>{'   '.repeat(BYTES_PER_LINE - lineBytes)}</span>);
    }

    // ASCII section separator
    lineElements.push(<span key={`sep-${line}`}> |</span>);

    // ASCII section - group consecutive chars with same highlight state
    let currentAsciiChunk = '';
    let currentAsciiHighlight = false;

    for (let i = 0; i <= lineBytes; i++) {
      const byteIndex = offset + i;
      const elementIndex = Math.floor(byteIndex / eltWidth);
      const shouldHighlight = isStruct && elementIndex % 2 === 1;

      // If we've hit the end or highlight state changed, save the current chunk
      if (i === lineBytes || (i > 0 && shouldHighlight !== currentAsciiHighlight)) {
        if (currentAsciiChunk) {
          lineElements.push(
            <span
              key={`ascii-${line}-${i}`}
              style={currentAsciiHighlight ? {backgroundColor: HIGHLIGHT_COLOR} : undefined}>
              {currentAsciiChunk}
            </span>,
          );
        }
        currentAsciiChunk = '';
        currentAsciiHighlight = shouldHighlight;
      }

      if (i < lineBytes) {
        const c = bytes[byteIndex];
        currentAsciiChunk += c >= 0x20 && c <= 0x7e ? String.fromCharCode(c) : '.';
      }
    }

    lineElements.push(<span key={`end-${line}`}>|</span>);

    lines.push(
      <div key={`line-${line}`} style={{display: 'flex'}}>
        {lineElements}
      </div>,
    );
  }

  const footer =
    totalLines > linesToShow ? <div key="footer">{`... (${totalLines - linesToShow} more lines)`}</div> : null;

  return (
    <div>
      <div>{header}</div>
      {lines}
      {footer}
    </div>
  );
}

// Format numeric values (for numeric streams)
function formatNumericPreview(values: number[], eltWidth: number): ReactNode {
  if (!values || values.length === 0) {
    return 'Stream Preview (numeric - 0 elements)';
  }

  let overflow = false;
  const lines: string[] = [];
  lines.push(`Stream Preview (numeric - eltWidth: ${eltWidth})`);

  let currentLine = '';
  let lineCount = 0;
  // Store each line until max character length is reached, then start a new line
  for (let i = 0; i < values.length; i++) {
    currentLine += values[i] + ' ';
    if (currentLine.length >= MAX_STRING_LEN) {
      lines.push(currentLine);
      currentLine = '';
      lineCount++;
    }

    if (lineCount >= MAX_LINES) {
      const remaining = values.length - i - 1;
      if (remaining > 0) {
        lines.push('...(' + remaining + ' more numbers)');
        overflow = true;
      }
      break;
    }
  }

  if (!overflow && currentLine.length !== 0) {
    lines.push(currentLine);
  }

  return <>{lines.join('\n')}</>;
}

// Format string values (for string streams)
function formatStringPreview(strings: string[]): ReactNode {
  if (!strings || strings.length === 0) {
    return 'Stream Preview (string - empty)';
  }

  const lines: string[] = [];
  lines.push(`Stream Preview (string - ${strings.length} elements)`);

  const stringsToShow = Math.min(MAX_LINES, strings.length);

  for (let i = 0; i < stringsToShow; i++) {
    let currentLine = strings[i];

    if (currentLine.length > MAX_STRING_LEN) {
      currentLine = strings[i].substring(0, MAX_STRING_LEN - 3) + '...';
    }

    lines.push(`[${i}]: "${currentLine}"`);
  }

  if (strings.length > stringsToShow) {
    lines.push(`... (${strings.length - stringsToShow} more strings)`);
  }

  return <>{lines.join('\n')}</>;
}

// Format preview based on stream type
function formatPreview(edge: InternalEdge): ReactNode | null {
  const {type, eltWidth, streamPreview} = edge;

  if (!streamPreview || streamPreview.length === 0) {
    return null;
  }

  switch (type) {
    case ZL_Type.ZL_Type_string:
      return formatStringPreview(streamPreview as string[]);
    case ZL_Type.ZL_Type_numeric:
      return formatNumericPreview(streamPreview as number[], eltWidth);
    case ZL_Type.ZL_Type_struct:
      return formatSerialPreview(streamPreview as number[], eltWidth, true);
    case ZL_Type.ZL_Type_serial:
    default:
      return formatSerialPreview(streamPreview as number[], eltWidth, false);
  }
}

export function EdgeView({data}: EdgeViewProps) {
  const edge = data.internalNode;
  let typeAndWidthInfo = edge.streamTypeToString();
  if (edge.type === ZL_Type.ZL_Type_numeric || edge.type === ZL_Type.ZL_Type_struct) {
    typeAndWidthInfo += `[${edge.eltWidth}]`;
  }

  const formattedPreview = formatPreview(edge);

  return (
    <Box
      bg={edge.inLargestCompressionPath ? '#d9ffee' : '#ffffffb0'}
      borderWidth={edge.inLargestCompressionPath ? '7px' : '1px'}
      borderColor={edge.inLargestCompressionPath ? '#2ed78b' : 'black'}
      p={'15px'}
      color={'black'}
      textAlign="center">
      <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
      <Handle type="source" position={Position.Bottom} id="source" style={{background: '#555'}} />
      <p style={{fontSize: '20px'}}>
        <b>{`#${edge.outputIdx} (${edge.rfid}) | ${typeAndWidthInfo}`}</b>
      </p>
      <Separator />
      <p>{`${edge.cSize} [${edge.share.toFixed(2)}%]`}</p>
      <p>{`${edge.numElts} elts`}</p>

      {formattedPreview && (
        <>
          <Float placement="top-end" offsetX={3} offsetY={3}>
            <Popover.Root positioning={{placement: 'top', flip: false, slide: false, offset: {crossAxis: -90}}}>
              <Popover.Trigger asChild>
                <Box
                  as="button"
                  cursor="pointer"
                  p="0px"
                  borderRadius="full"
                  _hover={{bg: 'gray.100'}}
                  aria-label="Stream Preview">
                  <MdInfoOutline size={20} />
                </Box>
              </Popover.Trigger>
              <Popover.Positioner>
                <Popover.Content width="auto">
                  <Popover.Arrow />
                  <Popover.Body>
                    <Box fontFamily="monospace" fontSize="12px" textAlign="left" whiteSpace="pre">
                      {formattedPreview}
                    </Box>
                  </Popover.Body>
                </Popover.Content>
              </Popover.Positioner>
            </Popover.Root>
          </Float>
        </>
      )}
    </Box>
  );
}
