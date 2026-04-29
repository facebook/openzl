// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useState, useRef, useCallback, useEffect} from 'react';
import './App.css';
import {extractStreamdumpFromCborFile} from './utils/decodeCbor.ts';
import {StreamdumpGraph} from './components/StreamdumpGraph.tsx';
import {Streamdump} from './models/Streamdump.ts';
import Toolbar from './components/Toolbar.tsx';
import {
  Box,
  createToaster,
  ToastCloseTrigger,
  ToastDescription,
  Toaster,
  ToastRoot,
  ToastTitle,
} from '@chakra-ui/react';

export const toaster = createToaster({placement: 'bottom-end'});

export default function App() {
  const [cborData, setCborData] = useState<Streamdump | null>(null);
  const [isTrackpadMode, setIsTrackpadMode] = useState(false);
  const [isKeyboardMode, setIsKeyboardMode] = useState(true);
  const toggleKeyboardNavRef = useRef<(() => void) | null>(null);

  const [fileInputKey, setFileInputKey] = useState<number>(0);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const toggleTrackpadMode = useCallback(() => {
    const next = !isTrackpadMode;
    setIsTrackpadMode(next);
    toaster.create({
      title: next ? 'Trackpad mode enabled' : 'Mouse mode enabled',
      type: 'info',
      duration: 1500,
    });
  }, [isTrackpadMode]);

  const toggleKeyboardNav = useCallback(() => {
    toggleKeyboardNavRef.current?.();
    const next = !isKeyboardMode;
    setIsKeyboardMode(next);
    toaster.create({
      title: next ? 'Keyboard shortcuts activated' : 'Keyboard shortcuts deactivated',
      description: next ? 'Please see the legend for details.' : '',
      type: 'info',
      duration: 1500,
    });
  }, [isKeyboardMode]);

  // Global key listeners for T and K shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.tagName === 'SELECT' || target.isContentEditable) {
        return;
      }
      switch (e.key.toLowerCase()) {
        case 't':
          e.preventDefault();
          toggleTrackpadMode();
          break;
        case 'k':
          e.preventDefault();
          toggleKeyboardNav();
          break;
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [toggleTrackpadMode, toggleKeyboardNav]);

  const handleFileChange = async (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) {
      console.log('No file uploaded yet!');
      return;
    }
    try {
      const data = await extractStreamdumpFromCborFile(file);
      setCborData(data);

      // Force a React re-render to update the input streamdump to decode and display
      setFileInputKey((prev) => prev + 1);
    } catch (err) {
      console.error('Failed to decode CBOR file:', err);
    }
  };

  // Used to input a new serialized CBOR file to display
  const handleFileButtonClick = () => {
    // Invoke receiving new input
    fileInputRef.current?.click();
  };

  return (
    <div className="wrapper">
      <Toaster toaster={toaster} style={{zIndex: 10000} /*Bring toaster to front*/}>
        {(toast) => (
          <ToastRoot minWidth="350px" flexDirection="column" alignItems="left">
            <ToastTitle>{toast.title as string}</ToastTitle>
            <ToastDescription>{toast.description as string}</ToastDescription>
            <ToastCloseTrigger />
          </ToastRoot>
        )}
      </Toaster>
      <Toolbar
        onUploadCborFile={handleFileButtonClick}
        onToggleTrackpadMode={toggleTrackpadMode}
        onToggleKeyboardNav={toggleKeyboardNav}
        isTrackpadMode={isTrackpadMode}
        isKeyboardMode={isKeyboardMode}
      />

      <div className="content">
        {/* Used to input a new file. This <input /> is needed, as a button in the toolbar invoke this input, to open file explorer and load new data once something is uploaded */}
        <input
          key={fileInputKey}
          ref={fileInputRef}
          type="file"
          onChange={handleFileChange}
          style={{display: 'none'}}
        />

        <Box h="100%" w="100%" paddingLeft={'5%'} paddingRight={'5%'} paddingTop={4} paddingBottom={4}>
          <StreamdumpGraph
            data={cborData}
            isTrackpadMode={isTrackpadMode}
            isKeyboardMode={isKeyboardMode}
            toggleKeyboardNavRef={toggleKeyboardNavRef}
          />
        </Box>
      </div>
    </div>
  );
}
