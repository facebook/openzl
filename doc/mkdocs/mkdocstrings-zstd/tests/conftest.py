from pathlib import Path
from typing import Iterator

import pytest
from markdown.core import Markdown
from mkdocs.config.defaults import MkDocsConfig
from mkdocstrings.plugin import MkdocstringsPlugin
from mkdocstrings_handlers.zstd.doxygen import Doxygen
from mkdocstrings_handlers.zstd.handler import ZstdHandler

from . import helpers


@pytest.fixture(name="doxygen")
def fixture_doxygen(tmp_path: Path) -> Doxygen:
    """Return a Doxygen instance.

    Returns:
        A Doxygen instance.
    """

    return helpers.doxygen(tmp_path)


@pytest.fixture(name="mkdocs_conf")
def fixture_mkdocs_conf(
    request: pytest.FixtureRequest, tmp_path: Path
) -> Iterator[MkDocsConfig]:
    """Yield a MkDocs configuration object.

    Parameters:
        request: Pytest fixture.
        tmp_path: Pytest fixture.

    Yields:
        MkDocs config.
    """
    with helpers.mkdocs_conf(request, tmp_path) as mkdocs_conf:
        yield mkdocs_conf


@pytest.fixture(name="plugin")
def fixture_plugin(mkdocs_conf: MkDocsConfig) -> MkdocstringsPlugin:
    """Return a plugin instance.

    Parameters:
        mkdocs_conf: Pytest fixture (see conftest.py).

    Returns:
        mkdocstrings plugin instance.
    """
    return helpers.plugin(mkdocs_conf)


@pytest.fixture(name="ext_markdown")
def fixture_ext_markdown(mkdocs_conf: MkDocsConfig) -> Markdown:
    """Return a Markdown instance with MkdocstringsExtension.

    Parameters:
        mkdocs_conf: Pytest fixture (see conftest.py).

    Returns:
        A Markdown instance.
    """
    return helpers.ext_markdown(mkdocs_conf)


@pytest.fixture(name="handler")
def fixture_handler(
    plugin: MkdocstringsPlugin, ext_markdown: Markdown, tmp_path: Path
) -> ZstdHandler:
    """Return a handler instance.

    Parameters:
        plugin: Pytest fixture (see conftest.py).

    Returns:
        A handler instance.
    """
    return helpers.handler(plugin, ext_markdown, tmp_path)
