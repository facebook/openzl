// Copyright (c) Meta Platforms, Inc. and affiliates.

'use strict';

/** The header's navigation links, so Help offers the same destinations. */
const HELP_LINKS = [
  {label: 'OpenZL Website', url: 'https://openzl.org/'},
  {label: 'Getting Started', url: 'https://openzl.org/getting-started/quick-start/'},
  {label: 'API Reference', url: 'https://openzl.org/api/c/compressor/'},
];

/**
 * The application menu. The standard roles give each platform its native
 * layout and shortcuts; on macOS, without an Edit menu, copy and paste do not
 * work in text fields at all. Reload and developer tools are left out of
 * packaged builds, as the window offers no other way to reach them.
 */
function menuTemplate({platform, isPackaged, openExternal}) {
  const developerItems = isPackaged
    ? []
    : [{role: 'reload'}, {role: 'forceReload'}, {role: 'toggleDevTools'}, {type: 'separator'}];
  return [
    ...(platform === 'darwin' ? [{role: 'appMenu'}] : []),
    {role: 'fileMenu'},
    {role: 'editMenu'},
    {
      label: 'View',
      submenu: [
        ...developerItems,
        {role: 'resetZoom'},
        {role: 'zoomIn'},
        {role: 'zoomOut'},
        {type: 'separator'},
        {role: 'togglefullscreen'},
      ],
    },
    {role: 'windowMenu'},
    {
      role: 'help',
      submenu: HELP_LINKS.map(({label, url}) => ({label, click: () => openExternal(url)})),
    },
  ];
}

module.exports = {HELP_LINKS, menuTemplate};
