// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it, vi} from 'vitest';
import {HELP_LINKS, menuTemplate, type MenuItemTemplate} from '../electron/app-menu.cjs';
import {isAllowedExternalUrl} from '../electron/external-links.cjs';

function roles(items: MenuItemTemplate[]) {
  return items.map((item) => item.role ?? item.label);
}

function viewItems(template: MenuItemTemplate[]) {
  const view = template.find((item) => item.label === 'View');
  return roles(view?.submenu ?? []);
}

describe('menuTemplate', () => {
  it('adds the app menu only on macOS', () => {
    const options = {isPackaged: true, openExternal: vi.fn()};
    expect(roles(menuTemplate({...options, platform: 'darwin'}))).toEqual([
      'appMenu',
      'fileMenu',
      'editMenu',
      'View',
      'windowMenu',
      'help',
    ]);
    for (const platform of ['win32', 'linux']) {
      expect(roles(menuTemplate({...options, platform}))).toEqual([
        'fileMenu',
        'editMenu',
        'View',
        'windowMenu',
        'help',
      ]);
    }
  });

  it('offers reload and developer tools only when unpackaged', () => {
    const options = {platform: 'linux', openExternal: vi.fn()};
    const developerRoles = ['reload', 'forceReload', 'toggleDevTools'];
    const packaged = viewItems(menuTemplate({...options, isPackaged: true}));
    const unpackaged = viewItems(menuTemplate({...options, isPackaged: false}));
    expect(packaged.filter((role) => role !== undefined && developerRoles.includes(role))).toEqual([]);
    expect(unpackaged).toEqual(expect.arrayContaining(developerRoles));
  });

  it('opens each help link externally', () => {
    const openExternal = vi.fn();
    const help = menuTemplate({platform: 'darwin', isPackaged: true, openExternal}).find(
      (item) => item.role === 'help',
    );
    const items = help?.submenu ?? [];
    expect(items.map((item) => item.label)).toEqual(HELP_LINKS.map((link) => link.label));
    for (const item of items) {
      item.click?.();
    }
    expect(openExternal.mock.calls.map(([url]) => url)).toEqual(HELP_LINKS.map((link) => link.url));
  });

  it('links only where the window itself may navigate', () => {
    for (const {url} of HELP_LINKS) {
      expect(isAllowedExternalUrl(url)).toBe(true);
    }
  });
});
