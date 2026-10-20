// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const childProcess = require('node:child_process');
const path = require('node:path');

/**
 * On Windows `zli` reads its arguments in the ANSI code page, not as UTF-16 or
 * UTF-8, so a path with a character outside that code page reaches it mangled
 * (or best-fit mapped to a different file). These are the code pages this
 * knows how to check, as the encoding labels TextDecoder understands.
 */
const ENCODING = {
  874: 'windows-874',
  932: 'shift_jis',
  936: 'gbk',
  949: 'euc-kr',
  950: 'big5',
  1250: 'windows-1250',
  1251: 'windows-1251',
  1252: 'windows-1252',
  1253: 'windows-1253',
  1254: 'windows-1254',
  1255: 'windows-1255',
  1256: 'windows-1256',
  1257: 'windows-1257',
  1258: 'windows-1258',
};
const DOUBLE_BYTE = new Set([932, 936, 949, 950]);
/**
 * Code pages whose TextDecoder table is not reliably Windows' own: `gbk` is
 * GB18030, whose 0xA8BC is ḿ in newer ICU where CP936 has a private-use
 * character; `euc-kr` lacks CP949's extended Hangul in some ICU builds; and
 * `big5` includes HKSCS characters CP950 does not have. What the decoder says
 * these can encode is a guess, so a path is never refused for them, and one
 * with anything past ASCII is always copied.
 */
const APPROXIMATE = new Set([936, 949, 950]);
const UTF8 = 65001;

/**
 * Windows-1252's 0x80-0x9F, which Node's TextDecoder gets wrong: it treats the
 * label as Latin-1 and returns C1 controls there instead of `€ ‘ ’ “ ” – —` and
 * the rest. Undefined bytes are left as the control character.
 */
const WINDOWS_1252_HIGH =
  '\u20ac\u0081\u201a\u0192\u201e\u2026\u2020\u2021\u02c6\u2030\u0160\u2039\u0152\u008d\u017d\u008f' +
  '\u0090\u2018\u2019\u201c\u201d\u2022\u2013\u2014\u02dc\u2122\u0161\u203a\u0153\u009d\u017e\u0178';

const representable = new Map();

/**
 * Every character the code page can encode, found by decoding its byte
 * sequences, or null if this runtime cannot decode it (a Node built with small
 * ICU has no `shift_jis`, for one).
 */
function charactersOf(codePage) {
  const cached = representable.get(codePage);
  if (cached !== undefined) {
    return cached;
  }
  const characters = new Set();
  if (codePage === 1252) {
    for (let byte = 0; byte < 256; byte += 1) {
      const character = byte >= 0x80 && byte <= 0x9f ? WINDOWS_1252_HIGH[byte - 0x80] : String.fromCharCode(byte);
      if (!/[\u0080-\u009f]/.test(character)) {
        characters.add(character);
      }
    }
    representable.set(codePage, characters);
    return characters;
  }
  let decoder;
  try {
    decoder = new TextDecoder(ENCODING[codePage], {fatal: true});
  } catch {
    representable.set(codePage, null);
    return null;
  }
  const add = (bytes) => {
    try {
      for (const character of decoder.decode(bytes)) {
        // Bytes the code page leaves undefined come back as C1 controls.
        if (!/[\u0080-\u009f]/.test(character)) {
          characters.add(character);
        }
      }
    } catch {
      // Not a complete character: a lead byte on its own, or an invalid pair.
    }
  };
  for (let byte = 0; byte < 256; byte += 1) {
    add(Uint8Array.of(byte));
  }
  if (DOUBLE_BYTE.has(codePage)) {
    for (let lead = 0x81; lead <= 0xfe; lead += 1) {
      for (let trail = 0x40; trail <= 0xfe; trail += 1) {
        add(Uint8Array.of(lead, trail));
      }
    }
  }
  representable.set(codePage, characters);
  return characters;
}

// Printable ASCII is in every code page here, so most paths skip the lookup.
const PRINTABLE_ASCII = /^[ -~]*$/;

/**
 * The characters of `text` that `codePage` cannot encode. Empty for a code page
 * it does not know, cannot decode, or cannot decode exactly (APPROXIMATE), or
 * for UTF-8: refusing what might work
 * would be worse than letting zli report its own error.
 */
function unrepresentableCharacters(text, codePage) {
  if (PRINTABLE_ASCII.test(text) || codePage === UTF8 || !(codePage in ENCODING) || APPROXIMATE.has(codePage)) {
    return [];
  }
  const known = charactersOf(codePage);
  return known === null ? [] : [...new Set([...text].filter((character) => !known.has(character)))];
}

/**
 * Whether zli might not get `text` intact, so a file at that path should be
 * copied to one it can read. Copying an input costs nothing when it turns out
 * not to be needed, so unlike refusing, this assumes the worst: a code page it
 * cannot check (unknown, unread, undecodable or APPROXIMATE) counts as having
 * nothing past ASCII.
 */
function mayNotReachZliIntact(text, codePage) {
  if (PRINTABLE_ASCII.test(text) || codePage === UTF8) {
    return false;
  }
  if (
    codePage === undefined ||
    !(codePage in ENCODING) ||
    APPROXIMATE.has(codePage) ||
    charactersOf(codePage) === null
  ) {
    return true;
  }
  return unrepresentableCharacters(text, codePage).length > 0;
}

/** Throws a message the page can show when `text` has characters `codePage` cannot encode. */
function assertReadableByZli(text, what, fix, codePage) {
  const bad = unrepresentableCharacters(text, codePage);
  if (bad.length === 0) {
    return;
  }
  const shown = bad
    .slice(0, 3)
    .map((character) => `"${character}"`)
    .join(', ');
  throw new Error(
    `${what} contains ${shown}${bad.length > 3 ? ' and more' : ''}, which this PC's ANSI code page (${codePage}) cannot represent, and the OpenZL command-line tool reads paths in that code page. ${fix}`,
  );
}

/** The ACP value in `reg query` output, or undefined. */
function parseCodePage(output) {
  // Anchored to the value name: the key also holds OEMCP and MACCP, whose
  // names end in ACP.
  const match = /^\s*ACP\s+REG_SZ\s+(\d+)\s*$/m.exec(output);
  return match === null ? undefined : Number(match[1]);
}

let detected;

/** The ACP value from the registry, or undefined if it cannot be read. */
function readCodePage() {
  return new Promise((resolve) => {
    childProcess.execFile(
      // Absolute: Windows resolves a bare name through the current directory
      // before PATH, so a stray reg.exe there would run instead.
      path.win32.join(process.env.SystemRoot ?? 'C:\\Windows', 'System32', 'reg.exe'),
      ['query', 'HKLM\\SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage', '/v', 'ACP'],
      {windowsHide: true},
      (error, stdout) => {
        resolve(error === null ? parseCodePage(stdout) : undefined);
      },
    );
  });
}

/**
 * This PC's ANSI code page; undefined if it cannot be read. Callers share one
 * read, and only a successful one is kept, so a failed read is retried.
 */
function windowsAnsiCodePage() {
  detected ??= readCodePage().then((codePage) => {
    if (codePage === undefined) {
      detected = undefined;
    }
    return codePage;
  });
  return detected;
}

module.exports = {
  assertReadableByZli,
  mayNotReachZliIntact,
  parseCodePage,
  unrepresentableCharacters,
  windowsAnsiCodePage,
};
