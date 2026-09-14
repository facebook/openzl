// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useRef, useState} from 'react';
import {Box, IconButton, Input, Text, VStack} from '@chakra-ui/react';
import {LuCloudUpload, LuX} from 'react-icons/lu';
import StepCard from './StepCard.tsx';

const FILE_SIZE_UNITS = ['B', 'KB', 'MB', 'GB', 'TB'] as const;

/**
 * Decimal units, not binary ones: the sample button next to this card promises
 * "5 MB", and a 5,000,000-byte file has to read back as 5 MB rather than 4.8.
 */
function formatFileSize(bytes: number): string {
  let size = bytes;
  let unit = 0;
  while (size >= 1000 && unit < FILE_SIZE_UNITS.length - 1) {
    size /= 1000;
    unit += 1;
  }
  const rounded = unit === 0 || size >= 10 ? Math.round(size) : Math.round(size * 10) / 10;
  return `${rounded} ${FILE_SIZE_UNITS[unit]}`;
}

interface UploadCardProps {
  file: File | null;
  onFileChange: (file: File | null) => void;
}

export default function UploadCard({file, onFileChange}: UploadCardProps) {
  const [isDragging, setIsDragging] = useState(false);
  const inputRef = useRef<HTMLInputElement | null>(null);

  const clearSelection = () => {
    onFileChange(null);
    // Reset the input so picking the same file again still fires onChange.
    if (inputRef.current) {
      inputRef.current.value = '';
    }
  };

  return (
    <StepCard
      number={1}
      title="Choose your data"
      subtitle="Choose data to compress, files are not uploaded to a server">
      {/* The drop target is this row rather than the label below it, so releasing
          over the remove button still lands a file instead of letting the browser
          navigate away from the page. */}
      <Box
        data-testid="upload-dropzone"
        // Exposed so the drag highlight, which is otherwise only a themed border
        // colour, can be asserted in tests.
        data-dragging={isDragging ? 'true' : undefined}
        display="flex"
        alignItems="center"
        gap="8px"
        bg={isDragging ? 'pg.accentBg' : 'pg.canvas'}
        borderWidth="1px"
        borderStyle="dashed"
        borderColor={isDragging ? 'pg.accent' : 'pg.border'}
        borderRadius="8px"
        pr={file ? '12px' : '0'}
        transition="background-color 120ms ease, border-color 120ms ease"
        _hover={{borderColor: 'pg.accent'}}
        _focusWithin={{outline: '2px solid', outlineColor: 'pg.accent', outlineOffset: '2px'}}
        onDragEnter={(event) => {
          event.preventDefault();
          setIsDragging(true);
        }}
        onDragOver={(event) => {
          event.preventDefault();
          event.dataTransfer.dropEffect = 'copy';
        }}
        onDragLeave={(event) => {
          // Moving onto a child fires dragleave on this element too; only clear
          // the highlight once the pointer has actually left the row.
          if (!event.currentTarget.contains(event.relatedTarget as Node | null)) {
            setIsDragging(false);
          }
        }}
        onDrop={(event) => {
          event.preventDefault();
          setIsDragging(false);
          // Chrome hands over a dropped directory as a zero-byte File, so the
          // entry API is the only way to tell it from a genuinely empty file.
          const entry = event.dataTransfer.items?.[0]?.webkitGetAsEntry?.();
          if (entry != null && !entry.isFile) {
            return;
          }
          const dropped = event.dataTransfer.files?.[0];
          if (dropped) {
            onFileChange(dropped);
          }
        }}>
        <VStack as="label" flex="1" minW={0} gap="8px" px="16px" py="32px" cursor="pointer">
          <Input
            ref={inputRef}
            type="file"
            srOnly
            onChange={(event) => {
              onFileChange(event.target.files?.[0] ?? null);
            }}
          />
          <Box aria-hidden="true" color="pg.muted">
            <LuCloudUpload size={32} />
          </Box>
          <Text
            as="span"
            maxW="100%"
            color="pg.ink"
            fontSize="14px"
            fontWeight="semibold"
            lineHeight="1.4"
            overflowWrap="anywhere"
            m={0}>
            {file?.name ?? 'Drag & drop a file here'}
          </Text>
          <Text as="span" color="pg.secondary" fontSize="13px" lineHeight="1.4" m={0}>
            {file === null ? (
              <>
                or{' '}
                <Text as="span" color="pg.accent" textDecoration="underline" textUnderlinePosition="from-font">
                  browse your computer
                </Text>
              </>
            ) : (
              <>
                {formatFileSize(file.size)} ·{' '}
                <Text as="span" color="pg.accent" textDecoration="underline" textUnderlinePosition="from-font">
                  Choose another file
                </Text>
              </>
            )}
          </Text>
        </VStack>

        {file !== null && (
          <IconButton
            aria-label={`Remove ${file.name}`}
            variant="ghost"
            size="xs"
            flexShrink={0}
            color="pg.muted"
            _hover={{color: 'pg.accent'}}
            onClick={clearSelection}>
            <LuX />
          </IconButton>
        )}
      </Box>

      {/* Announced separately: a live region inside the label would compete with
          the label text for the file input's accessible name. */}
      <Box srOnly aria-live="polite">
        {file === null ? 'No file selected' : `${file.name}, ${formatFileSize(file.size)} selected`}
      </Box>
    </StepCard>
  );
}
