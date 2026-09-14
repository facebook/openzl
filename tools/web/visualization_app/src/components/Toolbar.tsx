// Copyright (c) Meta Platforms, Inc. and affiliates.

import '../styles/toolbar.css';
import {Legend} from './Legend';
import {SettingsPanel} from './Settings';
import {ToolHeader} from '@openzl/web-common';
import logoUrl from '/OpenZL_logo.png?url';

interface ToolbarProps {
  onUploadCborFile: () => void;
  onToggleTrackpadMode: () => void;
  onToggleKeyboardNav: () => void;
  isTrackpadMode: boolean;
  isKeyboardMode: boolean;
}

const Toolbar: React.FC<ToolbarProps> = (props: ToolbarProps) => {
  return (
    <ToolHeader
      title="Trace Visualization"
      logoSrc={logoUrl}
      utilityActions={
        <div className="toolbar-icons">
          <SettingsPanel
            onToggleTrackpadMode={props.onToggleTrackpadMode}
            onToggleKeyboardNav={props.onToggleKeyboardNav}
            isTrackpadMode={props.isTrackpadMode}
            isKeyboardMode={props.isKeyboardMode}
          />
          <Legend />
        </div>
      }
      primaryAction={
        <button className="toolbar-button" type="button" onClick={props.onUploadCborFile}>
          <span className="toolbar-button-text">UPLOAD CBOR FILE</span>
        </button>
      }
    />
  );
};

export default Toolbar;
