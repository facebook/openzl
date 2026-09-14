// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {ReactNode} from 'react';
import './ToolHeader.css';

export interface ToolHeaderLink {
  label: string;
  href: string;
}

export const OPENZL_NAVIGATION: readonly ToolHeaderLink[] = [
  {label: 'HOME', href: '/#'},
  {label: 'API REFERENCE', href: '/api/c/compressor/'},
  {label: 'GETTING STARTED', href: '/getting-started/quick-start/'},
];

export interface ToolHeaderProps {
  title: string;
  logoSrc: string;
  homeHref?: string;
  navigation?: readonly ToolHeaderLink[];
  utilityActions?: ReactNode;
  primaryAction?: ReactNode;
}

export function ToolHeader({
  title,
  logoSrc,
  homeHref = '/#',
  navigation = OPENZL_NAVIGATION,
  utilityActions,
  primaryAction,
}: ToolHeaderProps) {
  return (
    <header className="tool-header">
      <div className="tool-header__top-row">
        <div className="tool-header__identity">
          <a className="tool-header__logo-link" href={homeHref} aria-label="OpenZL home">
            <img className="tool-header__logo" src={logoSrc} alt="OpenZL" />
          </a>
          <h1 className="tool-header__title">{title}</h1>
        </div>
        {utilityActions != null && <div className="tool-header__utility-actions">{utilityActions}</div>}
      </div>
      <div className="tool-header__bottom-row">
        <nav className="tool-header__navigation" aria-label="OpenZL">
          {navigation.map(({label, href}) => (
            <div className="tool-header__nav-item" key={label}>
              <a className="tool-header__nav-link" href={href}>
                {label}
              </a>
            </div>
          ))}
        </nav>
        {primaryAction != null && <div className="tool-header__primary-action">{primaryAction}</div>}
      </div>
    </header>
  );
}
