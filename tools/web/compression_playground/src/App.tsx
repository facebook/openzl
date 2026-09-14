// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Banner, ToolHeader} from '@openzl/web-common';
import './App.css';
import logoUrl from '/OpenZL_logo.png?url';

export default function App() {
  return (
    <div className="app-shell">
      <ToolHeader title="Compression Playground" logoSrc={logoUrl} />
      <main className="app">
        <Banner>Work in progress...</Banner>
      </main>
    </div>
  );
}
