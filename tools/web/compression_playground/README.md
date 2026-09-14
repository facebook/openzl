# OpenZL Compression Playground

Boilerplate for the OpenZL Compression Playground web tool. Part of the Yarn workspace in `dev/tools/web`.

## Development

From `dev/tools/web`:

```bash
yarn install
yarn workspace @openzl/compression-playground dev      # localhost dev server
yarn workspace @openzl/compression-playground build    # production build
yarn build                                              # build all tools
```

Shared code lives in `@openzl/web-common`. Shared build config (TypeScript, ESLint, Prettier) lives at the workspace root (`dev/tools/web`).

See `visualization_app/README.md` for general workspace conventions.
