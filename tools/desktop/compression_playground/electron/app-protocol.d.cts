// Copyright (c) Meta Platforms, Inc. and affiliates.

export const APP_ORIGIN: string;
export const APP_SCHEME: string;
export function originOf(url: string): string | undefined;
export function resolveBundlePath(root: string, requestUrl: string): string | undefined;
