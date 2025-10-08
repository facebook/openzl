// Copyright (c) Meta Platforms, Inc. and affiliates.

import '../styles/toolbar.css';
import {Legend} from './Legend';
import logoUrl from '/OpenZL_logo.png?url';
import {Box, Flex, HStack, VStack, Heading} from '@chakra-ui/react';

interface ToolbarProps {
  onUploadCborFile: () => void;
}

interface Props {
  key: string;
  name: string;
  link: string;
}

const Links = [
  {name: 'HOME', link: '/#'},
  {name: 'API REFERENCE', link: '/api/compressor'},
  {
    name: 'GETTING STARTED',
    link: '/getting-started/quick-start/',
  },
];

const NavLink = (props: Props) => {
  const {name, link} = props;
  return (
    <Box rounded={'md'} className="toolbar-button">
      <a href={link} className="toolbar-button-text">
        {name}
      </a>
    </Box>
  );
};

const Toolbar: React.FC<ToolbarProps> = (props: ToolbarProps) => {
  return (
    <Box bg="#23554a" px={4}>
      <VStack align="left" gap={0}>
        <Flex h={12} alignItems={'center'} justifyContent={'space-between'}>
          <HStack gap={4}>
            <Box>
              <a href="/#">
                <img src={logoUrl} alt="OpenZL Logo" className="toolbar-logo" />
              </a>
            </Box>
            <Heading size="xl" color="white" textAlign="center">
              Trace Visualization
            </Heading>
          </HStack>
          <Legend />
        </Flex>
        <Flex h={10} alignItems={'center'} justifyContent={'space-between'}>
          <HStack as={'nav'} gap={4} display={{base: 'none', md: 'flex'}}>
            {Links.map((link) => (
              <NavLink key={link.name} name={link.name} link={link.link} />
            ))}
          </HStack>
          <Box as="button" className="toolbar-button" onClick={props.onUploadCborFile}>
            <p className="toolbar-button-text">UPLOAD CBOR FILE</p>
          </Box>
        </Flex>
      </VStack>
    </Box>
  );
};

export default Toolbar;
