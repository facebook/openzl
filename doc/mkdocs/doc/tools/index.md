---
hide:
  - navigation
  - toc
---

<div class="openzl-tools-landing">
  <header class="openzl-tools-landing__intro">
    <h1>Tools</h1>
    <p>Explore OpenZL interactively. Visualize compression graphs, benchmark OpenZL against your own data and compare results through interactive charts.</p>
  </header>

  <div class="openzl-tools-grid">
    <section class="openzl-tool-card" aria-labelledby="trace-visualization-title">
      <div class="openzl-tool-card__copy">
        <h2 id="trace-visualization-title">Trace Visualization</h2>
        <p>Visualize and inspect OpenZL compression graphs to see how codecs are composed for your data.</p>
      </div>

      <div class="openzl-tool-card__trace-preview" aria-hidden="true"></div>

      <a class="openzl-tool-card__button" href="/tools/trace/">
        Launch Visualizer
        <svg aria-hidden="true" viewBox="0 0 16 16">
          <path d="M3.5 8h9M8.5 4l4 4-4 4" />
        </svg>
      </a>
    </section>

    <section class="openzl-tool-card" aria-labelledby="compression-benchmark-title">
      <div class="openzl-tool-card__copy">
        <h2 id="compression-benchmark-title">Compression Benchmark</h2>
        <p>Benchmark compression methods on your own files and compare speed vs. size trade-offs through interactive charts.</p>
      </div>

      <div class="openzl-benchmark-preview" aria-hidden="true">
        <span class="openzl-benchmark-preview__label">Placeholder Image</span>
        <div class="openzl-benchmark-preview__metric">
          <div class="openzl-benchmark-preview__row">
            <span class="openzl-benchmark-preview__badge">TBD</span>
            <span class="openzl-benchmark-preview__ratio">Ratio: 3.5x <span>(40 KB)</span></span>
          </div>
          <div class="openzl-benchmark-preview__track">
            <span></span>
          </div>
        </div>
      </div>

      <a class="openzl-tool-card__button" href="/tools/playground/">
        Open Benchmark
        <svg aria-hidden="true" viewBox="0 0 16 16">
          <path d="M3.5 8h9M8.5 4l4 4-4 4" />
        </svg>
      </a>
    </section>
  </div>
</div>
