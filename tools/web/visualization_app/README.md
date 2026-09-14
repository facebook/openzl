# React + TypeScript + Vite

This project uses [Vite](https://vitejs.dev/) and [TypeScript](http://www.typescriptlang.org/) [React](https://react.dev/).

The app is part of the Yarn workspace in `dev/tools`. Shared non-visual code can be added to `@openzl/web-common` when a concrete reuse case emerges; UI components remain local to each tool. Run `yarn install`, `yarn build`, and `yarn test:run` from `dev/tools`.

## Package Versioning

To prevent auto-updating to vulnerable packages, **always** pin to a specific `x.y.z` patch release of a package. **Never** use `^` or `~`.

## Linting

The project uses [ESLint](https://eslint.org/) for linting and [Prettier](https://prettier.io/) for formatting. You can run the linter using the following command

```
yarn lint
```

VSCode integration is supported through the [VSCode ESLint extension](https://marketplace.visualstudio.com/items?itemName=dbaeumer.vscode-eslint). The project is configured to display lint errors for JS/X, TS/X, and CSS; as well as auto-format on save.
