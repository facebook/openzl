import {Box, Flex, HStack, IconButton, NumberInput, Pagination} from '@chakra-ui/react';
import {Handle, Position} from '@xyflow/react';
import {HiChevronLeft, HiChevronRight} from 'react-icons/hi';
import {LocalParamsPopover, renderLocalParams} from './LocalParamsView';
import {useEffect, useState} from 'react';
import type {InternalSegmenterNode} from '../models/InternalSegmenterNode';

interface SegmentPickerProps {
  numChunks: number;
  onSetVisibleChunk: (chunkNum: number) => void;
}

const NumberPicker = (props: {defaultNum: number; maxValue: number; onChange: (e: number) => void}) => {
  const [num, setNum] = useState('0');
  useEffect(() => {
    if (props.defaultNum !== parseInt(num)) {
      setNum(props.defaultNum.toString());
    }
  }, [props.defaultNum]);
  const clamp = (num: number) => {
    return Math.min(Math.max(num, 0), props.maxValue);
  };
  const onConfirm = (val: string) => {
    const interimVal = clamp(parseInt(val));
    setNum(interimVal.toString());
    props.onChange(interimVal);
  };
  return (
    <NumberInput.Root
      w="60px"
      value={num}
      onValueChange={(e) => {
        setNum(e.value);
      }}
      onKeyDown={(e) => {
        if (e.key === 'Enter') {
          onConfirm(num);
        }
      }}
      onBlur={() => {
        onConfirm(num);
      }}
      min={0}
      max={props.maxValue}>
      {/* <NumberInput.Control /> */}
      <NumberInput.Input />
    </NumberInput.Root>
  );
};

const SegmentPicker = (props: SegmentPickerProps) => {
  const [pageNum, setPageNum] = useState(0);
  // see if it's a duplicate or if we need to actually display a new chunk
  const onNewChunkRequested = (chunkNum: number) => {
    if (chunkNum === pageNum) {
      return;
    }
    setPageNum(chunkNum);
    props.onSetVisibleChunk(chunkNum);
  };
  return (
    <Box paddingLeft="30px" paddingRight="30px">
      <Pagination.Root
        count={props.numChunks}
        pageSize={1}
        page={pageNum}
        onPageChange={(details) => {
          onNewChunkRequested(details.page);
        }}>
        <Flex justify="space-between">
          <Pagination.PrevTrigger asChild>
            <IconButton variant="ghost">
              <HiChevronLeft />
            </IconButton>
          </Pagination.PrevTrigger>
          <HStack gap={4}>
            <p>Chunk</p>
            <NumberPicker defaultNum={pageNum} maxValue={props.numChunks} onChange={onNewChunkRequested} />
            <p>of {props.numChunks}</p>
          </HStack>
          {/* <Pagination.PageText /> */}
          <Pagination.NextTrigger asChild>
            <IconButton variant="ghost">
              <HiChevronRight />
            </IconButton>
          </Pagination.NextTrigger>
        </Flex>
      </Pagination.Root>
    </Box>
  );
};

interface SegmenterViewProps {
  data: {
    internalNode: InternalSegmenterNode;
    onSetVisibleChunk: (chunkNum: number) => void;
  };
}

export function SegmenterNode({data}: SegmenterViewProps) {
  const segmenter = data.internalNode;
  const [showLocalParams] = useState(false);

  return (
    <div className={'segmenter-node'}>
      <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
      {segmenter.cLocalParams.hasLocalParams() && <LocalParamsPopover localParams={segmenter.cLocalParams} />}
      <div className="node-header">
        {segmenter.name} ({segmenter.cID}) ({segmenter.rfid})
      </div>
      <div className="node-content">
        <div>
          {segmenter.segmenterTypeToString()} | {segmenter.cHeaderSize}
        </div>
        {showLocalParams && renderLocalParams(segmenter.cLocalParams)}
      </div>
      <SegmentPicker numChunks={segmenter.numChunks} onSetVisibleChunk={data.onSetVisibleChunk} />
      {/* Output edge handle declaration for a node*/}
      <Handle type="source" position={Position.Bottom} id="source" style={{background: '#555'}} />
    </div>
  );
}
