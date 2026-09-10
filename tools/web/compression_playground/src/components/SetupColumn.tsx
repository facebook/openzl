// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Heading, Text, VStack} from '@chakra-ui/react';

interface SetupStep {
  number: number;
  title: string;
  subtitle: string;
}

const SETUP_STEPS: readonly SetupStep[] = [
  {
    number: 1,
    title: 'Choose your data',
    subtitle: 'Choose data to compress, files are not uploaded to a server',
  },
  {
    number: 2,
    title: 'Configure the run',
    subtitle: 'Select which compressors to compare',
  },
  {
    number: 3,
    title: 'Run the benchmark',
    subtitle: 'Run benchmarks',
  },
];

export default function SetupColumn() {
  return (
    <VStack gap="20px" width={{base: '100%', lg: '520px'}} flexShrink={0} align="stretch">
      {SETUP_STEPS.map((step) => (
        <Box
          key={step.number}
          as="section"
          aria-labelledby={`step-${step.number}-title`}
          bg="pg.surface"
          borderWidth="1px"
          borderColor="pg.border"
          borderRadius="12px"
          p="24px">
          <VStack gap="6px" align="stretch">
            <Box display="flex" alignItems="center" gap="10px">
              <Box
                aria-hidden="true"
                display="inline-flex"
                alignItems="center"
                justifyContent="center"
                boxSize="24px"
                borderRadius="full"
                bg="pg.ink"
                color="pg.onInk"
                fontSize="12px"
                fontWeight="bold"
                lineHeight="1">
                {step.number}
              </Box>
              <Heading
                id={`step-${step.number}-title`}
                as="h2"
                flex="1"
                color="pg.ink"
                fontSize="18px"
                fontWeight="bold"
                lineHeight="1.3"
                m={0}>
                {step.title}
              </Heading>
            </Box>
            <Text color="pg.secondary" fontSize="13px" lineHeight="1.4" m={0}>
              {step.subtitle}
            </Text>
          </VStack>
        </Box>
      ))}
    </VStack>
  );
}
