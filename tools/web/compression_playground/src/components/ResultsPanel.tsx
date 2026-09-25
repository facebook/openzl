// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Heading, Table, Text, VStack} from '@chakra-ui/react';
import {LuArrowRight, LuChartLine, LuChartSpline, LuMicroscope, LuMonitor} from 'react-icons/lu';
import {isRunInProgress, type BenchmarkJob, type RunState} from '../benchmarkTypes.ts';
import {WASM_PROFILE} from '../wasmProfiles.ts';

const HOW_IT_WORKS_STEPS = ['Choose your data.', 'Pick compressors to compare.', 'Run the benchmark.'];

interface ResultsPanelProps {
  runState: RunState;
}

function statusLine(runState: RunState): string | null {
  switch (runState.status) {
    case 'idle':
      return null;
    case 'loading':
      return 'Loading the OpenZL module…';
    case 'running':
      return `Running ${String(runState.completedJobs)} of ${String(runState.totalJobs)}…`;
    case 'completed':
      return `${String(runState.results.length)} measured`;
    case 'error':
      return `Run failed: ${runState.message}`;
  }
}

/** `idle` and `loading` carry no outcome, so there is nothing to list yet. */
function outcomeOf(runState: RunState) {
  return runState.status === 'idle' || runState.status === 'loading' ? null : runState;
}

function hasMeasurements(runState: RunState): boolean {
  const outcome = outcomeOf(runState);
  return outcome !== null && (outcome.results.length > 0 || outcome.failures.length > 0);
}

function describeJob(job: BenchmarkJob): string {
  if (job.compressor !== 'OpenZL') {
    return `${job.compressor} ${String(job.level)}`;
  }
  // `job.profile` crossed the seam as the binding's numeric enum, so read the
  // name back off the same table that put it there.
  const name = Object.entries(WASM_PROFILE).find(([, value]) => value === job.profile)?.[0] ?? String(job.profile);
  return `${job.compressor} ${name} / ${String(job.level)}`;
}

/**
 * NOT THE SHIPPING UI. A placeholder the measurements table replaces whole, and
 * the only way to see that a run really happens in a worker -- nothing below
 * this layer can be tested without a browser. It has none of the grouping,
 * sorting, bars or tags the design calls for, and is not laid out to any frame.
 */
function MeasurementList({runState}: ResultsPanelProps) {
  const outcome = outcomeOf(runState);
  if (outcome === null) {
    return null;
  }
  return (
    <Table.Root size="sm">
      <Table.Header>
        <Table.Row>
          <Table.ColumnHeader>Codec</Table.ColumnHeader>
          <Table.ColumnHeader>Compressed</Table.ColumnHeader>
          <Table.ColumnHeader>Ratio</Table.ColumnHeader>
          <Table.ColumnHeader>Compress</Table.ColumnHeader>
          <Table.ColumnHeader>Decompress</Table.ColumnHeader>
        </Table.Row>
      </Table.Header>
      <Table.Body>
        {outcome.results.map((result) => (
          // A trained job posts one result per candidate, all carrying its id.
          <Table.Row key={`${result.job.id}-${String(result.candidate?.index ?? 0)}`}>
            <Table.Cell>{describeJob(result.job)}</Table.Cell>
            <Table.Cell>{result.compressedSize.toLocaleString()} B</Table.Cell>
            <Table.Cell>{result.ratio.toFixed(2)}×</Table.Cell>
            <Table.Cell>{Math.round(result.compressMBps)} MB/s</Table.Cell>
            <Table.Cell>{Math.round(result.decompressMBps)} MB/s</Table.Cell>
          </Table.Row>
        ))}
        {outcome.failures.map((failure) => (
          <Table.Row key={failure.job.id}>
            <Table.Cell>{describeJob(failure.job)}</Table.Cell>
            <Table.Cell colSpan={4} color="pg.secondary">
              {failure.message}
            </Table.Cell>
          </Table.Row>
        ))}
      </Table.Body>
    </Table.Root>
  );
}

export default function ResultsPanel({runState}: ResultsPanelProps) {
  return (
    <Box
      as="section"
      aria-labelledby="results-title"
      aria-busy={isRunInProgress(runState)}
      flex="1"
      minW={0}
      alignSelf={{base: 'stretch', lg: 'flex-start'}}
      bg="pg.surface"
      borderWidth="1px"
      borderColor="pg.border"
      borderRadius="12px"
      p="32px">
      <VStack gap="24px" align="stretch">
        <Heading id="results-title" as="h2" color="pg.ink" fontSize="20px" fontWeight="extrabold" m={0}>
          Results
        </Heading>

        {/* Rendered even when it says nothing, so a screen reader has the
            region before the text arrives: one added at the same moment as its
            own text usually goes unannounced. `srOnly` takes it out of the
            column's flow rather than leaving a gap where no line is. A failure
            interrupts instead of waiting for a pause, hence `assertive`. */}
        <Text
          role="status"
          aria-live={runState.status === 'error' ? 'assertive' : 'polite'}
          srOnly={statusLine(runState) === null}
          color="pg.ink"
          fontSize="13px"
          fontWeight="semibold"
          m={0}>
          {statusLine(runState)}
        </Text>
        {hasMeasurements(runState) && <MeasurementList runState={runState} />}

        {/* Only while there is nothing to show: otherwise the page says it has
            no results directly under the ones it just listed. */}
        {!hasMeasurements(runState) && (
          <Box
            display="flex"
            alignItems="center"
            gap="16px"
            bg="pg.canvas"
            borderWidth="1px"
            borderColor="pg.border"
            borderRadius="8px"
            p="20px">
            <Box
              aria-hidden="true"
              display="inline-flex"
              alignItems="center"
              justifyContent="center"
              boxSize="40px"
              flexShrink={0}
              borderRadius="full"
              bg="pg.chip"
              color="pg.faint">
              <LuChartLine size={20} />
            </Box>
            <VStack gap="2px" flex="1" minW={0} align="stretch">
              <Text color="pg.ink" fontSize="15px" fontWeight="bold" m={0}>
                No results yet
              </Text>
              <Text color="pg.secondary" fontSize="14px" lineHeight="1.4" m={0}>
                Select your data to compress or hit &ldquo;Try a 5 MB sample&rdquo; to see the playground work.
              </Text>
            </VStack>
          </Box>
        )}

        <VStack gap="16px" align="stretch">
          <Heading
            as="h3"
            color="pg.muted"
            fontSize="12px"
            fontWeight="bold"
            textTransform="uppercase"
            letterSpacing="0.04em"
            m={0}>
            How it works
          </Heading>
          <VStack as="ol" gap="14px" align="stretch" m={0} p={0} listStyleType="none">
            {HOW_IT_WORKS_STEPS.map((step, index) => (
              <Box key={step} as="li" display="flex" alignItems="flex-start" gap="12px">
                <Box
                  aria-hidden="true"
                  display="inline-flex"
                  alignItems="center"
                  justifyContent="center"
                  boxSize="20px"
                  flexShrink={0}
                  borderRadius="full"
                  bg="pg.accentBg"
                  color="pg.accent"
                  fontSize="11px"
                  fontWeight="bold">
                  {index + 1}
                </Box>
                <Text color="pg.ink" fontSize="14px" fontWeight="bold" lineHeight="1.5" m={0}>
                  {step}
                </Text>
              </Box>
            ))}
          </VStack>
        </VStack>

        <VStack gap="16px" align="stretch">
          <Heading
            as="h3"
            color="pg.muted"
            fontSize="12px"
            fontWeight="bold"
            textTransform="uppercase"
            letterSpacing="0.04em"
            m={0}>
            Reading the results
          </Heading>
          <VStack as="ul" gap="14px" align="stretch" m={0} p={0} listStyleType="none">
            <Box as="li" display="flex" alignItems="flex-start" gap="12px">
              <Box
                aria-hidden="true"
                display="inline-flex"
                alignItems="center"
                justifyContent="center"
                boxSize="20px"
                flexShrink={0}
                color="pg.success">
                <LuChartSpline size={14} />
              </Box>
              <Text color="pg.secondary" fontSize="14px" lineHeight="1.5" m={0}>
                <Text as="span" color="pg.ink" fontWeight="bold">
                  What the charts say.{' '}
                </Text>
                These charts compare compression ratio against speed. The left chart shows compression speed, the right
                shows decompression speed. Points toward the top-right perform best. Hover over a point to see details,
                or click a row in the benchmarking table to explore its compression graph through the visualization
                tool.
              </Text>
            </Box>
            <Box as="li" display="flex" alignItems="flex-start" gap="12px">
              <Box
                aria-hidden="true"
                display="inline-flex"
                alignItems="center"
                justifyContent="center"
                boxSize="20px"
                flexShrink={0}
                color="pg.success">
                <LuMicroscope size={14} />
              </Box>
              <Text color="pg.secondary" fontSize="14px" lineHeight="1.5" m={0}>
                <Text as="span" color="pg.ink" fontWeight="bold">
                  Train an OpenZL compressor.{' '}
                </Text>
                Select &ldquo;Train&rdquo; on any OpenZL row in the setup to build a custom compressor from your data. A
                Pareto frontier will be shown, representing a set of compressors at different speed vs ratio trade-offs.
              </Text>
            </Box>
          </VStack>
        </VStack>

        <Box as="aside" aria-label="Desktop app recommendation" bg="pg.callout" borderRadius="12px" p="24px">
          <VStack gap="12px" align="stretch">
            <Box display="flex" alignItems="center" gap="10px" color="pg.ink" fontSize="15px" fontWeight="bold">
              <LuMonitor size={18} aria-hidden="true" />
              Get large files? Use the desktop app.
            </Box>
            <Text color="pg.ink" fontSize="14px" lineHeight="1.5" opacity={0.9} m={0}>
              The <Text as="strong">OpenZL Playground desktop app</Text> runs these exact benchmarks entirely on your
              own machine: it reads files straight off disk, so there is no practical size limit, training runs and
              Pareto frontiers all stay local.
            </Text>
            {/* Text, not Link, until the desktop app has a URL: an anchor with
                no href gets neither the link role nor keyboard focus, so it
                would only look interactive. */}
            <Text color="pg.accent" fontSize="14px" fontWeight="bold" m={0}>
              Use the desktop app <LuArrowRight size={14} aria-hidden="true" />
            </Text>
          </VStack>
        </Box>
      </VStack>
    </Box>
  );
}
