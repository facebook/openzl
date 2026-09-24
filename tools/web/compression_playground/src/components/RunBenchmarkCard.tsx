// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Button} from '@chakra-ui/react';
import {LuPlay} from 'react-icons/lu';
import StepCard from './StepCard.tsx';
import {isRunInProgress, type RunConfig, type RunState} from '../benchmarkTypes.ts';

interface RunBenchmarkCardProps {
  runConfig: RunConfig | null;
  runState: RunState;
  onRun: ((config: RunConfig) => void) | null;
  onTrySample: (() => void) | null;
}

export default function RunBenchmarkCard({runConfig, runState, onRun, onTrySample}: RunBenchmarkCardProps) {
  const isBusy = isRunInProgress(runState);
  const canRun = runConfig !== null && onRun !== null && !isBusy;
  const canTrySample = onTrySample !== null && !isBusy;

  return (
    <StepCard number={3} title="Run the benchmark" subtitle="Measure ratio and speed for every compressor you selected">
      <Box display="flex" gap="12px" alignItems="center">
        <Button
          type="button"
          flex="1"
          minW={0}
          bg="pg.primary"
          color="white"
          fontSize="14px"
          fontWeight="bold"
          px="24px"
          py="12px"
          borderRadius="8px"
          _hover={{bg: 'pg.primaryHover'}}
          disabled={!canRun}
          onClick={() => {
            if (canRun) {
              onRun(runConfig);
            }
          }}>
          <LuPlay aria-hidden="true" />
          Run benchmark
        </Button>
        <Button
          type="button"
          flex="1"
          minW={0}
          variant="outline"
          bg="pg.surface"
          borderColor="pg.border"
          color="pg.secondary"
          fontSize="14px"
          fontWeight="semibold"
          px="16px"
          py="12px"
          borderRadius="8px"
          _hover={{bg: 'pg.canvas'}}
          disabled={!canTrySample}
          onClick={onTrySample ?? undefined}>
          Try a 5 MB sample
        </Button>
      </Box>
    </StepCard>
  );
}
