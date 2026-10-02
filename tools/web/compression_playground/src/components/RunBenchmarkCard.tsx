// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Button, Text, VStack, chakra} from '@chakra-ui/react';
import {LuCircleAlert, LuCircleCheck, LuLoaderCircle, LuPlay} from 'react-icons/lu';
import StepCard from './StepCard.tsx';
import {isRunInProgress, type RunConfig, type RunState} from '../benchmarkTypes.ts';
import {formatBytes, runSummary, type RunSummary} from '../measurements.ts';

/**
 * How far through the run, and what it is doing. A training job runs for
 * minutes and counts as one job, so the job counter alone would sit still for
 * most of a trained run; the trainer's own progress fills that stretch.
 */
function progressOf(runState: RunState): {fraction: number; label: string} | null {
  if (runState.status === 'loading') {
    return {fraction: 0, label: 'Loading the OpenZL module…'};
  }
  if (runState.status !== 'running') {
    return null;
  }
  const {completedJobs, totalJobs, step} = runState;
  const done = totalJobs === 0 ? 0 : completedJobs / totalJobs;
  // The last job's result and `finished` are two messages, so there is a frame
  // between them where every job is counted and the run is still `running`.
  const running = Math.min(completedJobs + 1, totalJobs);
  const label = `Running ${String(running)} out of ${String(totalJobs)}…`;
  if (step === null) {
    return {fraction: done, label};
  }
  // The step belongs to the job after the ones already counted, so its own
  // fraction advances the bar across that job's share and no further.
  const share = totalJobs === 0 ? 0 : 1 / totalJobs;
  return {fraction: Math.min(1, done + step * share), label};
}

function doneLine({srcSize, succeeded, failed}: RunSummary): string {
  const size = srcSize === null ? null : formatBytes(srcSize);
  const counts = `${String(succeeded)} succeeded, ${String(failed)} failed`;
  return size === null ? `Done — ${counts}` : `Done — ${size} · ${counts}`;
}

const Spinner = chakra(LuLoaderCircle, {
  base: {animation: 'spin 1s linear infinite'},
});

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
  const progress = progressOf(runState);
  const summary = runSummary(runState);

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
          // The design greys the button while it works rather than offering a
          // cancel, so the disabled state has to stay legible against white.
          _disabled={isBusy ? {bg: 'pg.primaryBusy', color: 'white', opacity: 1, cursor: 'wait'} : undefined}
          onClick={() => {
            if (canRun) {
              onRun(runConfig);
            }
          }}>
          {isBusy ? <Spinner aria-hidden="true" /> : <LuPlay aria-hidden="true" />}
          {isBusy ? 'Running…' : 'Run benchmark'}
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
      {/* Its own live region, because the progress one below unmounts at the
          same moment this appears: a region that goes away announces nothing,
          so the run would end in silence. `aria-atomic` so the whole line is
          read rather than whichever part changed. */}
      {summary !== null && (
        <Box display="flex" alignItems="center" gap="8px" pt="4px" role="status" aria-live="polite" aria-atomic="true">
          <Box
            aria-hidden="true"
            display="inline-flex"
            flexShrink={0}
            color={summary.failed > 0 ? 'pg.danger' : 'pg.successText'}>
            {summary.failed > 0 ? <LuCircleAlert size={16} /> : <LuCircleCheck size={16} />}
          </Box>
          <Text color={summary.failed > 0 ? 'pg.danger' : 'pg.successText'} fontSize="13px" fontWeight="medium" m={0}>
            {doneLine(summary)}
          </Text>
        </Box>
      )}
      {progress !== null && (
        <VStack gap="8px" align="stretch" role="status" aria-live="polite">
          <Text color="pg.ink" fontSize="13px" fontWeight="semibold" m={0}>
            {progress.label}
          </Text>
          <Box
            height="8px"
            borderRadius="4px"
            bg="pg.border"
            overflow="hidden"
            role="progressbar"
            aria-valuemin={0}
            aria-valuemax={100}
            aria-valuenow={Math.round(progress.fraction * 100)}>
            <Box
              height="100%"
              bg="pg.primary"
              width={`${String(Math.round(progress.fraction * 100))}%`}
              transition="width 120ms linear"
            />
          </Box>
        </VStack>
      )}
    </StepCard>
  );
}
