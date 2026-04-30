import os
from collections import ChainMap
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import Any

import pytest
from markdown.core import Markdown
from mkdocs.config.defaults import MkDocsConfig
from mkdocstrings.plugin import MkdocstringsPlugin
from mkdocstrings_handlers.zstd.doxygen import Doxygen
from mkdocstrings_handlers.zstd.handler import ZstdHandler


def src_dir():
    test_dir = Path(__file__).parent
    return test_dir / "src"


def sources():
    return [
        "file1.h",
        "file2.h",
        "dir/file3.h",
        "file5.hpp",
    ]


def doxygen(tmp_path: Path) -> Doxygen:
    """Return a Doxygen instance.

    Returns:
        A Dandler instance.
    """
    return Doxygen(
        source_directory=src_dir(),
        include_paths=["include"],
        xml_output=tmp_path,
        sources=sources(),
        predefined=[],
    )


@contextmanager
def mkdocs_conf(
    request: pytest.FixtureRequest, tmp_path: Path
) -> Iterator[MkDocsConfig]:
    """Yield a MkDocs configuration object.

    Parameters:
        request: Pytest request fixture.
        tmp_path: Temporary path.

    Yields:
        MkDocs config.
    """
    conf = MkDocsConfig()
    while hasattr(request, "_parent_request") and hasattr(
        request._parent_request, "_parent_request"
    ):
        request = request._parent_request

    conf_dict = {
        "site_name": "foo",
        "site_url": "https://example.org/",
        "site_dir": str(tmp_path / "site"),
        "plugins": [{"mkdocstrings": {"default_handler": "python"}}],
        "docs_dir": str(tmp_path / "docs"),
        "theme": "material",
        **getattr(request, "param", {}),
    }
    os.makedirs(conf_dict["docs_dir"], exist_ok=True)
    # Re-create it manually as a workaround for https://github.com/mkdocs/mkdocs/issues/2289
    mdx_configs: dict[str, Any] = dict(
        ChainMap(*conf_dict.get("markdown_extensions", []))
    )

    conf.load_dict(conf_dict)
    assert conf.validate() == ([], []), f"{conf.validate()}"

    conf["mdx_configs"] = mdx_configs
    conf["markdown_extensions"].insert(0, "toc")  # Guaranteed to be added by MkDocs.

    conf = conf["plugins"]["mkdocstrings"].on_config(conf)
    conf = conf["plugins"]["autorefs"].on_config(conf)
    yield conf
    conf["plugins"]["mkdocstrings"].on_post_build(conf)


def plugin(mkdocs_conf: MkDocsConfig) -> MkdocstringsPlugin:
    """Return a plugin instance.

    Parameters:
        mkdocs_conf: MkDocs configuration.

    Returns:
        mkdocstrings plugin instance.
    """
    return mkdocs_conf["plugins"]["mkdocstrings"]


def ext_markdown(mkdocs_conf: MkDocsConfig) -> Markdown:
    """Return a Markdown instance with MkdocstringsExtension.

    Parameters:
        mkdocs_conf: MkDocs configuration.

    Returns:
        A Markdown instance.
    """
    return Markdown(
        extensions=mkdocs_conf["markdown_extensions"],
        extension_configs=mkdocs_conf["mdx_configs"],
    )


def handler(
    plugin: MkdocstringsPlugin, ext_markdown: Markdown, tmp_path: Path
) -> ZstdHandler:
    """Return a handler instance.

    Parameters:
        plugin: Plugin instance.

    Returns:
        A handler instance.
    """

    handler = plugin.handlers.get_handler(
        "zstd",
        handler_config={
            "source_directory": src_dir(),
            "include_paths": ["include"],
            "build_directory": str(tmp_path / "build"),
            "sources": sources(),
        },
    )
    handler._update_env(ext_markdown, plugin.handlers._config)
    return handler  # type: ignore[return-value]
