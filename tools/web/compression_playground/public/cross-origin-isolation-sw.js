// Copyright (c) Meta Platforms, Inc. and affiliates.

// Makes the playground cross-origin isolated, which is what gates
// `SharedArrayBuffer` -- and the OpenZL WASM module is a pthreads build, so
// without it the module cannot start at all.
//
// Isolation normally comes from two response headers, but the site is served by
// GitHub Pages, which does not let us set any. A service worker can add them on
// the way out instead: once this worker controls the page, every response it
// passes through carries the headers, and the browser grants the capability.
//
// The file plays both roles. Loaded from a page it registers itself; running as
// a worker it rewrites responses. Keeping it in one file means the worker's URL
// is just this script's own URL, so the registration scope lands on the tool's
// directory without anyone having to hardcode the deploy path.

const COOP = 'same-origin';
// `credentialless` rather than `require-corp` so a cross-origin subresource
// without a CORP header still loads -- it is fetched without credentials
// instead of being blocked. The fonts this page pulls from Google do send CORP
// today, but that is their choice to revoke, not ours.
const COEP = 'credentialless';

// Statuses whose responses must not carry a body.
const NULL_BODY_STATUSES = new Set([204, 205, 304]);

// Set just before the reload below and cleared once the page comes back
// isolated, so one session can only ever spend one reload on this.
const RELOAD_KEY = 'openzl-coi-reloaded';

if (typeof window === 'undefined') {
  self.addEventListener('install', (event) => {
    // No waiting phase: the sooner this worker activates, the sooner the reload
    // below produces an isolated document. `skipWaiting` is a promise, and the
    // worker may be killed once the install handler returns, so hold the event
    // open until it settles.
    event.waitUntil(self.skipWaiting());
  });

  self.addEventListener('activate', (event) => {
    // Take over the page that registered us, which is what makes the very next
    // response go through the fetch handler.
    event.waitUntil(self.clients.claim());
  });

  self.addEventListener('fetch', (event) => {
    const request = event.request;

    // Chrome rejects `only-if-cached` requests that are not same-origin as soon
    // as a worker touches them, so leave those to the browser untouched.
    if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') {
      return;
    }

    event.respondWith(
      fetch(request)
        .then((response) => {
          // Rebuilding these is either impossible or illegal: an opaque
          // response has an immutable, empty header list, and a 204, 205 or 304
          // may not carry a body at all, so passing one to the Response
          // constructor throws. None of them can be isolated anyway.
          if (response.status === 0 || NULL_BODY_STATUSES.has(response.status)) {
            return response;
          }
          const headers = new Headers(response.headers);
          headers.set('Cross-Origin-Opener-Policy', COOP);
          headers.set('Cross-Origin-Embedder-Policy', COEP);
          return new Response(response.body, {
            status: response.status,
            statusText: response.statusText,
            headers,
          });
        })
        .catch((error) => {
          // The browser shows its network error page either way; this is only
          // so the reason is visible when the worker is in the path.
          console.error('OpenZL: fetch failed inside the isolation worker.', error);
          throw error;
        }),
    );
  });
} else {
  // `document.currentScript` is only readable while this script is executing,
  // so the URL is captured now rather than inside the callback below.
  //
  // Truthiness rather than definedness: it is null for a module script, and an
  // inline script has a non-null one whose `src` is empty. `register('')`
  // would resolve against the page and scope the worker to the wrong
  // directory, which is worse than not registering at all.
  const workerUrl = document.currentScript?.src;

  if (!workerUrl) {
    console.error(
      'OpenZL: the isolation worker can only register from a classic external script, so the WASM module cannot start.',
    );
  } else if (window.crossOriginIsolated) {
    // Already isolated, so either the worker is in place or real headers
    // arrived. Clear the guard so a future session may retry the reload.
    window.sessionStorage.removeItem(RELOAD_KEY);
  } else if (!('serviceWorker' in navigator)) {
    console.error('OpenZL: no service worker support, so the WASM module cannot start.');
  } else if (window.sessionStorage.getItem(RELOAD_KEY) !== null) {
    // Reloaded once already and still not isolated: the worker cannot fix this,
    // and reloading again would only loop.
    console.error('OpenZL: cross-origin isolation failed, so the WASM module cannot start.');
  } else {
    // A full reload, so anything the page is already holding is lost. It
    // happens within milliseconds of first load, long before someone could pick
    // a file, but that stops being true if the app ever starts work of its own
    // this early. Guarded so a worker that installs without isolating costs one
    // reload rather than an endless cycle of them.
    const reloadOnce = () => {
      if (window.sessionStorage.getItem(RELOAD_KEY) !== null) {
        return;
      }
      window.sessionStorage.setItem(RELOAD_KEY, '1');
      window.location.reload();
    };

    navigator.serviceWorker.addEventListener('controllerchange', reloadOnce);
    navigator.serviceWorker
      .register(workerUrl)
      .then((registration) => {
        // A worker that is already active and already controlling this document
        // will never fire `controllerchange`, so without this the page would sit
        // there unisolated and silent. It got here through the worker and came
        // back without the headers, which one more trip fixes.
        if (registration.active !== null && navigator.serviceWorker.controller !== null) {
          reloadOnce();
        }
      })
      .catch((error) => {
        console.error('OpenZL: registering the cross-origin isolation worker failed.', error);
      });
  }
}
