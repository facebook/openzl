// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useRef, useState} from 'react';
import {
  Box,
  Button,
  Checkbox,
  IconButton,
  NativeSelect,
  Portal,
  Select,
  Slider,
  Text,
  Tooltip,
  VStack,
  chakra,
  createListCollection,
} from '@chakra-ui/react';
import {LuChevronDown, LuChevronRight, LuInfo, LuPlus, LuX} from 'react-icons/lu';
import StepCard from './StepCard.tsx';
import {
  COMPRESSOR_NAMES,
  ITERATION_TICKS,
  ITERATIONS_DEFAULT,
  ITERATIONS_MAX,
  ITERATIONS_MIN,
  ITERATIONS_THUMB_HALF_PX,
  OPENZL_LEVELS,
  OPENZL_PROFILES,
  OPENZL_PROFILE_DESCRIPTIONS,
  TRAINED_CANDIDATE_COUNTS,
  createCompressorRow,
  levelsFor,
  tickLabelLeft,
  type CompressorName,
  type CompressorRow,
  type OpenZlProfile,
} from '../compressors.ts';

interface CompressorRowProps {
  row: CompressorRow;
  position: number;
  canRemove: boolean;
  onPatch: (id: number, patch: Partial<CompressorRow>) => void;
  onCompressorChange: (id: number, compressor: CompressorName) => void;
  onRemove: (id: number) => void;
}

function OpenZlOptions({row, position, onPatch}: Pick<CompressorRowProps, 'row' | 'position' | 'onPatch'>) {
  // The CLI cannot train a pytorch model, so the profile locks TRAIN off
  // rather than letting the run seam build a request the CLI rejects.
  const canTrain = row.profile !== 'pytorch';
  return (
    <Box borderRadius="6px" overflow="hidden">
      <chakra.button
        type="button"
        display="flex"
        gap="6px"
        alignItems="center"
        width="100%"
        px="10px"
        py="8px"
        bg="transparent"
        borderWidth="0"
        fontFamily="inherit"
        textAlign="left"
        cursor="pointer"
        aria-expanded={row.optionsOpen}
        aria-label={`OpenZL options for row ${position}`}
        onClick={() => onPatch(row.id, {optionsOpen: !row.optionsOpen})}>
        <Box aria-hidden="true" color="pg.secondary">
          {row.optionsOpen ? <LuChevronDown size={10} /> : <LuChevronRight size={10} />}
        </Box>
        <Text color="pg.secondary" fontSize="13px" fontWeight="medium" lineHeight="1" m={0}>
          OpenZL options
        </Text>
      </chakra.button>

      {row.optionsOpen && (
        <Box px="12px" py="10px">
          <VStack gap="10px" align="stretch">
            <Box display="flex" gap="8px" alignItems="center">
              <Text color="pg.faint" fontSize="11px" fontWeight="semibold" m={0}>
                LEVEL
              </Text>
              <NativeSelect.Root width="120px" flexShrink={0}>
                <NativeSelect.Field
                  aria-label={`OpenZL level for row ${position}`}
                  height="32px"
                  fontSize="13px"
                  bg="pg.surface"
                  borderColor="pg.border"
                  borderRadius="6px"
                  value={row.level}
                  onChange={(event) => onPatch(row.id, {level: Number(event.target.value)})}>
                  {OPENZL_LEVELS.map((level) => (
                    <option key={level} value={level}>
                      {level === 6 ? '6 (default)' : level}
                    </option>
                  ))}
                </NativeSelect.Field>
                <NativeSelect.Indicator />
              </NativeSelect.Root>
            </Box>

            <Box display="flex" alignItems="center" justifyContent="space-between" gap="8px">
              <Checkbox.Root
                checked={row.train && canTrain}
                disabled={!canTrain}
                onCheckedChange={(details) => onPatch(row.id, {train: details.checked === true})}>
                <Checkbox.HiddenInput />
                <Checkbox.Control
                  boxSize="16px"
                  borderRadius="4px"
                  bg="pg.surface"
                  borderColor="pg.border"
                  _checked={{bg: 'pg.accent', borderColor: 'pg.accent', color: 'white'}}
                />
                <Checkbox.Label color="pg.faint" fontSize="11px" fontWeight="semibold">
                  TRAIN
                </Checkbox.Label>
              </Checkbox.Root>

              <Box display="flex" alignItems="center" gap="8px">
                <Text color="pg.faint" fontSize="11px" fontWeight="semibold" textAlign="right" m={0}>
                  NUMBER OF TRAINED CANDIDATES
                </Text>
                <NativeSelect.Root width="72px" flexShrink={0} disabled={!row.train || !canTrain}>
                  <NativeSelect.Field
                    aria-label={`Number of trained candidates for row ${position}`}
                    height="28px"
                    fontSize="12px"
                    bg="pg.surface"
                    borderColor="pg.border"
                    borderRadius="6px"
                    value={row.candidates}
                    onChange={(event) => onPatch(row.id, {candidates: Number(event.target.value)})}>
                    {TRAINED_CANDIDATE_COUNTS.map((count) => (
                      <option key={count} value={count}>
                        {count}
                      </option>
                    ))}
                  </NativeSelect.Field>
                  <NativeSelect.Indicator />
                </NativeSelect.Root>
              </Box>
            </Box>

            <Box
              display="flex"
              gap="8px"
              alignItems="center"
              bg="pg.accentBg"
              borderWidth="1px"
              borderColor="pg.infoBorder"
              borderRadius="6px"
              px="10px"
              py="8px">
              <Box aria-hidden="true" color="pg.accent" flexShrink={0}>
                <LuInfo size={14} />
              </Box>
              <Text color="pg.secondary" fontSize="11px" lineHeight="1.4" m={0}>
                {canTrain
                  ? 'Training learns a compressor from your data'
                  : 'Training is not supported for the pytorch profile'}
              </Text>
            </Box>
          </VStack>
        </Box>
      )}
    </Box>
  );
}

const profileCollection = createListCollection<OpenZlProfile>({items: [...OPENZL_PROFILES]});

function ProfileSelect({row, position, onPatch}: Pick<CompressorRowProps, 'row' | 'position' | 'onPatch'>) {
  return (
    <Select.Root
      collection={profileCollection}
      size="sm"
      width="100px"
      flexShrink={0}
      value={[row.profile]}
      onValueChange={(details) => {
        const [next] = details.value;
        if (next != null) {
          // The value always comes from our own option list. Clearing `train`
          // here as well as disabling the checkbox keeps the stored row honest,
          // since the run seam reads state rather than the rendered control.
          const profile = next as OpenZlProfile;
          onPatch(row.id, {profile, ...(profile === 'pytorch' ? {train: false} : {})});
        }
      }}>
      <Select.Label srOnly>Level or profile for row {position}</Select.Label>
      {/* The indicator group must sit beside the trigger inside the control:
          it is absolutely positioned and the control is the only positioned
          ancestor — nested in the trigger, the arrow flies off-box. */}
      <Select.Control>
        <Select.Trigger bg="pg.surface" borderColor="pg.border" borderRadius="6px" px="12px" color="pg.ink">
          <Select.ValueText />
        </Select.Trigger>
        <Select.IndicatorGroup>
          <Select.Indicator />
        </Select.IndicatorGroup>
      </Select.Control>
      <Select.Positioner>
        <Select.Content color="pg.ink">
          {profileCollection.items.map((profile) => (
            <Select.Item key={profile} item={profile}>
              <Tooltip.Root openDelay={200} positioning={{placement: 'right', gutter: 8}}>
                <Tooltip.Trigger asChild>
                  <Select.ItemText>{profile}</Select.ItemText>
                </Tooltip.Trigger>
                {/* Portalled out: the listbox scrolls, so inline content would
                    be clipped by its overflow. */}
                <Portal>
                  <Tooltip.Positioner>
                    <Tooltip.Content>
                      <Tooltip.Arrow>
                        <Tooltip.ArrowTip />
                      </Tooltip.Arrow>
                      {OPENZL_PROFILE_DESCRIPTIONS[profile]}
                    </Tooltip.Content>
                  </Tooltip.Positioner>
                </Portal>
              </Tooltip.Root>
              <Select.ItemIndicator />
            </Select.Item>
          ))}
        </Select.Content>
      </Select.Positioner>
    </Select.Root>
  );
}

function CompressorRowCard({row, position, canRemove, onPatch, onCompressorChange, onRemove}: CompressorRowProps) {
  const isOpenZl = row.compressor === 'OpenZL';
  return (
    <Box
      bg="pg.canvas"
      borderWidth="1px"
      borderColor="pg.border"
      borderRadius="8px"
      px="12px"
      pt="12px"
      pb="8px"
      display="flex"
      flexDirection="column"
      gap="4px">
      <Box display="flex" gap="8px" alignItems="center">
        <NativeSelect.Root flex="1" minW={0}>
          <NativeSelect.Field
            aria-label={`Compressor for row ${position}`}
            height="36px"
            fontSize="14px"
            color="pg.ink"
            bg="pg.surface"
            borderColor="pg.border"
            borderRadius="6px"
            value={row.compressor}
            onChange={(event) => onCompressorChange(row.id, event.target.value as CompressorName)}>
            {COMPRESSOR_NAMES.map((name) => (
              <option key={name} value={name}>
                {name}
              </option>
            ))}
          </NativeSelect.Field>
          <NativeSelect.Indicator />
        </NativeSelect.Root>

        {isOpenZl ? (
          <ProfileSelect row={row} position={position} onPatch={onPatch} />
        ) : (
          <NativeSelect.Root width="100px" flexShrink={0}>
            <NativeSelect.Field
              aria-label={`Level or profile for row ${position}`}
              height="36px"
              fontSize="14px"
              color="pg.ink"
              bg="pg.surface"
              borderColor="pg.border"
              borderRadius="6px"
              value={row.level}
              onChange={(event) => {
                onPatch(row.id, {level: Number(event.target.value)});
              }}>
              {levelsFor(row.compressor).map((level) => (
                <option key={level} value={level}>
                  {level}
                </option>
              ))}
            </NativeSelect.Field>
            <NativeSelect.Indicator />
          </NativeSelect.Root>
        )}

        <IconButton
          aria-label={`Remove row ${position}`}
          variant="ghost"
          size="2xs"
          flexShrink={0}
          color="pg.muted"
          _hover={{color: 'pg.accent'}}
          disabled={!canRemove}
          onClick={() => onRemove(row.id)}>
          <LuX />
        </IconButton>
      </Box>

      {isOpenZl && <OpenZlOptions row={row} position={position} onPatch={onPatch} />}
    </Box>
  );
}

export default function ConfigureRunCard() {
  const [rows, setRows] = useState<readonly CompressorRow[]>(() => [
    createCompressorRow(1, 'OpenZL'),
    createCompressorRow(2, 'zstd'),
    createCompressorRow(3, 'gzip'),
  ]);
  const nextId = useRef(4);
  const [iterations, setIterations] = useState(ITERATIONS_DEFAULT);

  const patchRow = (id: number, patch: Partial<CompressorRow>) => {
    setRows((prev) => prev.map((row) => (row.id === id ? {...row, ...patch} : row)));
  };

  const changeCompressor = (id: number, compressor: CompressorName) => {
    // A fresh row resets the dependent fields: a zstd level of 19 is not a
    // valid OpenZL level, and a profile means nothing to gzip.
    setRows((prev) => prev.map((row) => (row.id === id ? createCompressorRow(id, compressor) : row)));
  };

  const addRow = () => {
    const row = createCompressorRow(nextId.current, 'OpenZL');
    nextId.current += 1;
    setRows((prev) => [...prev, row]);
  };

  const removeRow = (id: number) => {
    setRows((prev) => prev.filter((row) => row.id !== id));
  };

  return (
    <StepCard number={2} title="Configure the run" subtitle="Select which compressors to compare">
      <VStack gap="10px" align="stretch">
        <Box display="flex" gap="8px" height="20px" alignItems="start">
          <Text flex="1" color="pg.faint" fontSize="11px" fontWeight="semibold" m={0}>
            COMPRESSOR
          </Text>
          {/* Spans the level dropdown plus the delete button it sits above. */}
          <Text width="132px" flexShrink={0} color="pg.faint" fontSize="11px" fontWeight="semibold" m={0}>
            LEVEL / PROFILE
          </Text>
        </Box>
        {rows.map((row, index) => (
          <CompressorRowCard
            key={row.id}
            row={row}
            position={index + 1}
            canRemove={rows.length > 1}
            onPatch={patchRow}
            onCompressorChange={changeCompressor}
            onRemove={removeRow}
          />
        ))}
      </VStack>

      <Box>
        <Button
          type="button"
          variant="outline"
          size="sm"
          bg="pg.surface"
          borderColor="pg.border"
          color="pg.secondary"
          fontSize="13px"
          fontWeight="semibold"
          borderRadius="6px"
          _hover={{bg: 'pg.canvas'}}
          onClick={addRow}>
          <LuPlus />
          Add compressor
        </Button>
      </Box>

      <Box height="1px" bg="pg.border" />

      <VStack gap="10px" align="stretch">
        <Box display="flex" gap="8px" alignItems="baseline" flexWrap="wrap">
          <Text color="pg.ink" fontSize="11px" fontWeight="bold" m={0}>
            ITERATIONS
          </Text>
          <Text color="pg.muted" fontSize="12px" m={0}>
            repeat each measurement, report the mean
          </Text>
          <Box flex="1" />
          <Text color="pg.ink" fontSize="12px" fontWeight="bold" data-testid="iterations-value" m={0}>
            {iterations}
          </Text>
        </Box>
        <VStack gap="12px" align="stretch">
          <Slider.Root
            min={ITERATIONS_MIN}
            max={ITERATIONS_MAX}
            step={1}
            value={[iterations]}
            onValueChange={(details) => setIterations(details.value[0] ?? ITERATIONS_DEFAULT)}
            width="100%">
            {/* The thumb is named through this label: zag always sets
                aria-labelledby to the label id, which shadows aria-label. */}
            <Slider.Label srOnly>Iterations</Slider.Label>
            {/* The track and thumb must live inside the control: the root is a
                column flex, so a track placed directly in it collapses — its
                flex-basis of 0% overrides the explicit height. */}
            <Slider.Control data-testid="iterations-control">
              <Slider.Track bg="pg.border" height="4px" borderRadius="2px">
                <Slider.Range bg="pg.accent" />
              </Slider.Track>
              <Slider.Thumb
                index={0}
                bg="pg.accent"
                borderColor="pg.accent"
                boxSize={`${ITERATIONS_THUMB_HALF_PX * 2}px`}
                borderRadius="8px"
                shadow="sm">
                <Slider.HiddenInput />
              </Slider.Thumb>
            </Slider.Control>
          </Slider.Root>
          <Box position="relative" height="16px" data-testid="iteration-ticks">
            {ITERATION_TICKS.map((tick) => (
              <Text
                key={tick}
                position="absolute"
                top={0}
                left={tickLabelLeft(tick)}
                transform="translateX(-50%)"
                color="pg.muted"
                fontSize="11px"
                lineHeight="16px"
                m={0}>
                {tick}
              </Text>
            ))}
          </Box>
        </VStack>
      </VStack>
    </StepCard>
  );
}
