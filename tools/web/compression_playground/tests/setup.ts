// Copyright (c) Meta Platforms, Inc. and affiliates.

// jsdom omits several DOM APIs the Chakra/zag machines rely on to position
// overlays and scroll the selected option into view. Browsers provide all of
// these; the stubs below only exist so interaction tests can drive the real
// components under jsdom.
// Guarded because this file is also loaded for the node-environment suites,
// where `Element` does not exist at all.
if (typeof Element !== 'undefined') {
  Element.prototype.scrollTo = () => undefined;
  Element.prototype.scrollIntoView = () => undefined;
}

class ResizeObserverStub {
  observe() {
    return undefined;
  }
  unobserve() {
    return undefined;
  }
  disconnect() {
    return undefined;
  }
}
globalThis.ResizeObserver ??= ResizeObserverStub as unknown as typeof ResizeObserver;
