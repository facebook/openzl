// Copyright (c) Meta Platforms, Inc. and affiliates.

/** The subset of Electron's `MenuItemConstructorOptions` the template uses. */
export interface MenuItemTemplate {
  role?: string;
  label?: string;
  type?: 'separator';
  click?: () => void;
  submenu?: MenuItemTemplate[];
}

export const HELP_LINKS: readonly {label: string; url: string}[];

export function menuTemplate(options: {
  platform: string;
  isPackaged: boolean;
  openExternal: (url: string) => void;
}): MenuItemTemplate[];
