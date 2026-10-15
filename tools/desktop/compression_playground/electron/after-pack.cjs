// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

// electron-builder afterPack hook. It runs before code signing, so flipping
// fuses here does not invalidate a signature.

const path = require('node:path');
const {flipFuses, FuseV1Options, FuseVersion} = require('@electron/fuses');
const {Arch} = require('electron-builder');

function electronBinaryPath({appOutDir, electronPlatformName, packager}) {
  switch (electronPlatformName) {
    case 'darwin':
      return path.join(appOutDir, `${packager.appInfo.productFilename}.app`);
    case 'win32':
      return path.join(appOutDir, `${packager.appInfo.productFilename}.exe`);
    default:
      return path.join(appOutDir, packager.executableName);
  }
}

exports.default = async function afterPack(context) {
  await flipFuses(electronBinaryPath(context), {
    version: FuseVersion.V1,
    // Unsigned Apple Silicon builds must be re-signed ad hoc or they will not launch.
    resetAdHocDarwinSignature:
      context.electronPlatformName === 'darwin' && (context.arch === Arch.arm64 || context.arch === Arch.universal),
    [FuseV1Options.RunAsNode]: false,
    [FuseV1Options.EnableCookieEncryption]: true,
    [FuseV1Options.EnableNodeOptionsEnvironmentVariable]: false,
    [FuseV1Options.EnableNodeCliInspectArguments]: false,
    [FuseV1Options.OnlyLoadAppFromAsar]: true,
    [FuseV1Options.GrantFileProtocolExtraPrivileges]: false,
  });
};
