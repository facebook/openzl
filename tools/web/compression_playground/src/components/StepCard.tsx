// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Heading, Text, VStack} from '@chakra-ui/react';
import type {ReactNode} from 'react';

interface StepCardProps {
  number: number;
  title: string;
  subtitle: string;
  children?: ReactNode;
}

export default function StepCard({number, title, subtitle, children}: StepCardProps) {
  return (
    <Box
      as="section"
      aria-labelledby={`step-${number}-title`}
      bg="pg.surface"
      borderWidth="1px"
      borderColor="pg.border"
      borderRadius="12px"
      p="24px">
      <VStack gap="16px" align="stretch">
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
              {number}
            </Box>
            <Heading
              id={`step-${number}-title`}
              as="h2"
              flex="1"
              color="pg.ink"
              fontSize="18px"
              fontWeight="bold"
              lineHeight="1.3"
              m={0}>
              {title}
            </Heading>
          </Box>
          <Text color="pg.secondary" fontSize="13px" lineHeight="1.4" m={0}>
            {subtitle}
          </Text>
        </VStack>
        {children}
      </VStack>
    </Box>
  );
}
