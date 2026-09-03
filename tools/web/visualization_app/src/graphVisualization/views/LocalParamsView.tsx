// Copyright (c) Meta Platforms, Inc. and affiliates.

import {LocalParamInfo} from '../../models/LocalParamInfo';
import {useState} from 'react';
import {useFloating, autoUpdate} from '@floating-ui/react';
import {CiCircleMore} from '../../icons/TablerIcons';

interface LocalParamsPopoverProps {
  localParams: LocalParamInfo;
}

// Display local params
export function renderLocalParams(localParams: LocalParamInfo) {
  const {intParams, copyParams, refParams} = localParams;

  return (
    <div className="local-params-container">
      {intParams.length > 0 && (
        <div className="param-section">
          <div className="local-param-header">Int Parameters:</div>
          <div className="local-param-item">
            {intParams.map((param, index) => (
              <span key={index}>
                ({param.paramId}, {param.paramValue}){index < intParams.length - 1 ? ', ' : ''}
              </span>
            ))}
          </div>
        </div>
      )}

      {copyParams.length > 0 && (
        <div className="param-section">
          <div className="local-param-header">Copy Parameters:</div>
          <div className="local-param-item">
            {copyParams.map((param, index) => (
              <span key={index}>
                ({param.paramId}, {param.paramSize}, {param.paramData}){index < copyParams.length - 1 ? ', ' : ''}
              </span>
            ))}
          </div>
        </div>
      )}

      {refParams.length > 0 && (
        <div className="param-section">
          <div className="local-param-header">Ref Parameters:</div>
          <div className="local-param-item">
            {refParams.map((param, index) => (
              <span key={index}>
                ({param.paramId}){index < refParams.length - 1 ? ', ' : ''}
              </span>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}

export function LocalParamsPopover({localParams}: LocalParamsPopoverProps) {
  const [isOpen, setIsOpen] = useState(false);
  const {refs, floatingStyles} = useFloating({
    open: isOpen,
    onOpenChange: setIsOpen,
    placement: 'top',
    whileElementsMounted: autoUpdate,
  });

  return (
    <div className="local-params-popover-container">
      <div ref={refs.setReference} className="local-params-toggle" onClick={() => setIsOpen(!isOpen)}>
        <CiCircleMore />
      </div>

      {isOpen && (
        <div ref={refs.setFloating} style={floatingStyles} className="popover-content">
          {renderLocalParams(localParams)}
        </div>
      )}
    </div>
  );
}
