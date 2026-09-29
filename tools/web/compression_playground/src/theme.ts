// Copyright (c) Meta Platforms, Inc. and affiliates.

import {createSystem, defaultConfig, defineConfig} from '@chakra-ui/react';

/**
 * Compression Playground palette, sourced from the Figma file. Components
 * reference the role names (`color="pg.ink"`, `bg="pg.surface"`) instead of
 * hardcoded hex values.
 *
 * Three constraints here are easy to break:
 *
 * - Colours belong under `semanticTokens`, not `tokens`. The latter is the
 *   primitive layer and holds one value per entry, so a palette defined there
 *   cannot express dark mode at all.
 * - Dark variants hang off `_osDark`, which Chakra maps to
 *   `@media (prefers-color-scheme: dark)`. `_dark` keys off a colour-mode class
 *   instead and needs a provider this app does not mount, so it would silently
 *   never match.
 * - The `pg` prefix keeps these clear of Chakra's own semantic colours, which
 *   already claim bare names such as `border`, `bg` and `fg`.
 *
 * Dark values are derived rather than designed -- the Figma file only covers
 * light mode. Every foreground/background pair clears WCAG AA against the
 * surface it sits on. `pg.muted` is deliberately darker than Figma's #94a3b8:
 * that value reaches only 2.56:1, and it carries the 12px uppercase section
 * headings, which are too small to qualify for the large-text threshold.
 */
const playgroundConfig = defineConfig({
  theme: {
    semanticTokens: {
      colors: {
        pg: {
          // Surfaces
          pageBg: {value: {base: '#ffffff', _osDark: '#1f2129'}},
          surface: {value: {base: '#ffffff', _osDark: '#282b35'}},
          canvas: {value: {base: '#fafafb', _osDark: '#242731'}},
          chip: {value: {base: '#eceff1', _osDark: '#343844'}},
          callout: {value: {base: '#edf2ff', _osDark: '#252f4a'}},
          accentBg: {value: {base: '#eff6ff', _osDark: '#1e3a5f'}},
          border: {value: {base: '#e2e8f0', _osDark: '#424754'}},

          // Foregrounds. `ink` doubles as the step-badge fill, so it inverts
          // with the scheme and `onInk` inverts against it.
          ink: {value: {base: '#0f172a', _osDark: '#f1f5f9'}},
          onInk: {value: {base: '#ffffff', _osDark: '#0f172a'}},
          secondary: {value: {base: '#475569', _osDark: '#bfc1c6'}},
          muted: {value: {base: '#64748b', _osDark: '#93979f'}},
          faint: {value: {base: '#667385', _osDark: '#94a3b8'}},
          accent: {value: {base: '#2563eb', _osDark: '#60a5fa'}},
          success: {value: {base: '#059669', _osDark: '#34d399'}},

          // Step 2/3 controls. The run button is indigo rather than the blue
          // accent, and the training note pairs the accent wash with a blue
          // border instead of the neutral one.
          // The dark variant is darker than Figma's #6366f1: that only reaches
          // 4.47:1 behind the button's 14px bold white label.
          primary: {value: {base: '#4f46e5', _osDark: '#5b54ee'}},
          primaryHover: {value: {base: '#4338ca', _osDark: '#4f46e5'}},
          infoBorder: {value: {base: '#bfdbfe', _osDark: '#2f4a6b'}},
        },
      },
    },
    tokens: {
      fonts: {
        body: {
          value: 'Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif',
        },
        heading: {
          value: 'Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif',
        },
      },
    },
  },
});

export const playgroundSystem = createSystem(defaultConfig, playgroundConfig);
