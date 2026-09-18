// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Box, Button} from '@chakra-ui/react';
import {LuPlay} from 'react-icons/lu';
import StepCard from './StepCard.tsx';

export default function RunBenchmarkCard() {
  // Disabled until the run seam lands, which turns the chosen file plus the
  // step 2 settings into a `RunConfig`. The design shows both enabled; an
  // enabled button that silently does nothing reads as broken instead.
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
          disabled>
          <LuPlay />
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
          disabled>
          Try a 5 MB sample
        </Button>
      </Box>
    </StepCard>
  );
}
