// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Heading, Text, VStack} from '@chakra-ui/react';
import {LuArrowRight, LuChartLine, LuChartSpline, LuMicroscope, LuMonitor} from 'react-icons/lu';
import {isRunInProgress, type RunState} from '../benchmarkTypes.ts';

const HOW_IT_WORKS_STEPS = ['Choose your data.', 'Pick compressors to compare.', 'Run the benchmark.'];

interface ResultsPanelProps {
  runState: RunState;
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
