// Copyright (c) Meta Platforms, Inc. and affiliates.

import js from '@eslint/js';
import globals from 'globals';
import tseslint from 'typescript-eslint';
import pluginReact from 'eslint-plugin-react';
import json from '@eslint/json';
import css from '@eslint/css';
import pluginPrettier from 'eslint-plugin-prettier/recommended';
import {defineConfig} from 'eslint/config';

export default defineConfig([
  {
    ignores: ['node_modules/', 'dist/'],
  },
  {
    files: ['**/*.{js,mjs,cjs,ts,mts,cts,jsx,tsx}'],
    plugins: {js},
    extends: ['js/recommended'],
    languageOptions: {globals: {...globals.browser, ...globals.node}},
  },
  tseslint.configs.strict,
  tseslint.configs.stylistic,
  {...pluginReact.configs.flat.recommended, files: ['**/*.{jsx,tsx}']},
  {...pluginReact.configs.flat['jsx-runtime'], files: ['**/*.{jsx,tsx}']},
  {
    files: ['**/*.json'],
    ignores: ['package.json'],
    plugins: {json},
    language: 'json/json',
    extends: ['json/recommended'],
  },
  {files: ['**/*.css'], plugins: {css}, language: 'css/css', extends: ['css/recommended']},
  // prettier needs to be at the bottom
  pluginPrettier,
]);
