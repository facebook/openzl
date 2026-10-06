// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useId} from 'react';
import {Box, Button, Portal, Tooltip} from '@chakra-ui/react';
import {LuPlay} from 'react-icons/lu';
import StepCard from './StepCard.tsx';
import {isRunInProgress, type RunConfig, type RunState} from '../benchmarkTypes.ts';

interface RunBenchmarkCardProps {
  runConfig: RunConfig | null;
  runState: RunState;
  onRun: ((config: RunConfig) => void) | null;
  onTrySample: (() => void) | null;
  /** What stands between the current setup and a run, said in the order to fix it. */
  runBlockers: readonly string[];
}

export default function RunBenchmarkCard({
  runConfig,
  runState,
  onRun,
  onTrySample,
  runBlockers,
}: RunBenchmarkCardProps) {
  const isBusy = isRunInProgress(runState);
  const canRun = runConfig !== null && onRun !== null && !isBusy;
  const canTrySample = onTrySample !== null && !isBusy;
  // Not while a run is in flight: the status line already says why, and there
  // is nothing for the user to fix.
  const blockedReason = !canRun && !isBusy && runBlockers.length > 0 ? runBlockers.join(' ') : null;
  const blockedReasonId = useId();

  return (
    <StepCard number={3} title="Run the benchmark" subtitle="Measure ratio and speed for every compressor you selected">
      <Box display="flex" gap="12px" alignItems="center">
        <Tooltip.Root openDelay={200} disabled={blockedReason === null} positioning={{placement: 'top', gutter: 8}}>
          <Tooltip.Trigger asChild>
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
              _hover={canRun ? {bg: 'pg.primaryHover'} : undefined}
              // `aria-disabled` rather than `disabled` while there is a reason to
              // show: a disabled button gets no pointer events and no focus, so
              // the tooltip saying why could never open. Clicks are still
              // refused below.
              disabled={!canRun && blockedReason === null}
              aria-disabled={blockedReason !== null || undefined}
              aria-describedby={blockedReason === null ? undefined : blockedReasonId}
              onClick={() => {
                if (canRun) {
                  onRun(runConfig);
                }
              }}>
              <LuPlay aria-hidden="true" />
              Run benchmark
            </Button>
          </Tooltip.Trigger>
          <Portal>
            <Tooltip.Positioner>
              <Tooltip.Content>
                <Tooltip.Arrow>
                  <Tooltip.ArrowTip />
                </Tooltip.Arrow>
                {blockedReason}
              </Tooltip.Content>
            </Tooltip.Positioner>
          </Portal>
        </Tooltip.Root>
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
      {/* The tooltip only opens on hover or focus, so the reason is also kept
          here for a screen reader to announce with the button. */}
      {blockedReason !== null && (
        <Box id={blockedReasonId} srOnly>
          {blockedReason}
        </Box>
      )}
    </StepCard>
  );
}
