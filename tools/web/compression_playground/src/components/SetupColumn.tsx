// Copyright (c) Meta Platforms, Inc. and affiliates.

import {VStack} from '@chakra-ui/react';
import StepCard from './StepCard.tsx';
import UploadCard from './UploadCard.tsx';

interface SetupStep {
  number: number;
  title: string;
  subtitle: string;
}

const SETUP_STEPS: readonly SetupStep[] = [
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

interface SetupColumnProps {
  file: File | null;
  onFileChange: (file: File | null) => void;
}

export default function SetupColumn({file, onFileChange}: SetupColumnProps) {
  return (
    <VStack gap="20px" width={{base: '100%', lg: '520px'}} flexShrink={0} align="stretch">
      <UploadCard file={file} onFileChange={onFileChange} />
      {SETUP_STEPS.map((step) => (
        <StepCard key={step.number} number={step.number} title={step.title} subtitle={step.subtitle} />
      ))}
    </VStack>
  );
}
