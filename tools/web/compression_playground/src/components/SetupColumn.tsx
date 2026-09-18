// Copyright (c) Meta Platforms, Inc. and affiliates.

import {VStack} from '@chakra-ui/react';
import ConfigureRunCard from './ConfigureRunCard.tsx';
import RunBenchmarkCard from './RunBenchmarkCard.tsx';
import UploadCard from './UploadCard.tsx';

interface SetupColumnProps {
  file: File | null;
  onFileChange: (file: File | null) => void;
}

export default function SetupColumn({file, onFileChange}: SetupColumnProps) {
  return (
    <VStack gap="20px" width={{base: '100%', lg: '520px'}} flexShrink={0} align="stretch">
      <UploadCard file={file} onFileChange={onFileChange} />
      <ConfigureRunCard />
      <RunBenchmarkCard />
    </VStack>
  );
}
