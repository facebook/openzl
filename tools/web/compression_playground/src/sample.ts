// Copyright (c) Meta Platforms, Inc. and affiliates.

const WORDS = (
  'lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor incididunt ut labore et dolore magna ' +
  'aliqua enim ad minim veniam quis nostrud exercitation ullamco laboris nisi aliquip ex ea commodo consequat duis ' +
  'aute irure in reprehenderit voluptate velit esse cillum eu fugiat nulla pariatur excepteur sint occaecat cupidatat ' +
  'non proident sunt culpa qui officia deserunt mollit anim id est laborum'
).split(' ');

/** A small seeded generator, so every run of the tool measures the same bytes. */
function mulberry32(seed: number): () => number {
  let state = seed;
  return () => {
    state = (state + 0x6d2b79f5) | 0;
    let t = Math.imul(state ^ (state >>> 15), 1 | state);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/**
 * Lorem ipsum of the size asked for, the same bytes every time.
 *
 * Words are drawn at random rather than repeating one paragraph: a repeated
 * line compresses to almost nothing, which would make every ratio on the page
 * meaninglessly large. Shared by the browser and desktop builds so a sample
 * means the same data in both.
 */
export function makeSample(size = 5_000_000): File {
  const next = mulberry32(0x0b5e55ed);
  const words: string[] = ['Lorem', 'ipsum'];
  let length = 'Lorem ipsum'.length;
  while (length < size) {
    const word = WORDS[Math.floor(next() * WORDS.length)];
    words.push(word);
    length += word.length + 1;
  }
  const bytes = new TextEncoder().encode(words.join(' ')).slice(0, size);
  return new File([bytes], 'sample.txt', {type: 'text/plain'});
}
