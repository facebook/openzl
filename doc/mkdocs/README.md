# OpenZL mkdocs documentation

## Resources

For general mkdocs capabilities see:

* [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/reference/)
* [MkDocs](https://www.mkdocs.org/user-guide/writing-your-docs/)

For automatic Doxygen symbol documentation importing with:

```
::: ZS2_symbolName
```

See [mkdocstrings](https://mkdocstrings.github.io/usage/).

## Config

The website config is in `mkdocs.yml`. This controls things like the navigation bar.

## Building

In fbcode:

```
./static_docs_build_script build
```

In open source, first make sure these dependencies are installed:

* System: doxygen
* System: clang-format
* System: node
* System: yarn
* Python: mkdocs==1.6.1
* Python: mkdocstrings==0.27.0
* Python: mkdocs-material==9.5.50
* Python: mkdocstrings-python=1.13.0

Then run:

```
./github_pages_build_script build
```

## Deploying

This site will be deployed to GitHub pages at https://facebook.github.io/openzl auotmatically. It will also be automatically deployed to an internal site.

## Testing

Every internal diff & GitHub PR builds the docs & fails if the build fails. You can test manually by running the build script.

## Serving Locally

Run one of these commands:

```
./github_pages_build_script serve
./static_docs_build_script serve
```

## More capabilities

Run one of these commands:

```
./github_pages_build_script --help
./static_docs_build_script --help
```

## Example files

The `src/` symlink in this directory is necessary to include example files. These files are not included in the final site, but can be used for example files like so:

```
\`\`\` cpp title="examples/compress_app.cpp"
--8<-- "src/examples/compress_app.cpp"
\`\`\`
```
