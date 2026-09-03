# Copyright (c) Meta Platforms, Inc. and affiliates.

import json
from pathlib import Path
from unittest.mock import patch

import pytest
from mkdocs_openzl.plugin import (
    _dependency_packages,
    _workspace_root_inputs,
    OpenZLConfig,
    OpenZLPlugin,
    PythonBuilder,
    Stamp,
    WEB_TOOLS,
    WebToolBuilder,
    WebToolConfig,
)


class FakeConfig:
    """Minimal stand-in for MkDocsConfig: builders only read docs_dir and site_dir."""

    def __init__(self, docs_dir: Path, site_dir: Path):
        self.docs_dir = str(docs_dir)
        self.site_dir = str(site_dir)


def _create_workspace(root: Path, packages: dict[str, dict]) -> None:
    """Materializes a Yarn workspace root and its members.

    `packages` maps a member directory name to that member's package.json.
    Every builder test needs one, because WebToolBuilder resolves a tool's
    sibling-package dependencies through the workspace manifests.
    """
    root.mkdir(parents=True, exist_ok=True)
    (root / "package.json").write_text(json.dumps({"workspaces": sorted(packages)}))
    for directory, manifest in packages.items():
        member = root / directory
        member.mkdir(parents=True, exist_ok=True)
        (member / "package.json").write_text(json.dumps(manifest))


def _deep_docs_dir(tmp_path: Path) -> Path:
    """
    Returns a docs_dir nested 3 levels inside tmp_path, so a production
    src_relative like "../../../tools/web/visualization_app" resolves *inside*
    tmp_path. A shallow tmp_path/docs would resolve to /tmp/pytest-of-USER/tools/...,
    which is shared across tests and races when they run in parallel.
    """
    deep = tmp_path / "a" / "b" / "c" / "docs"
    deep.mkdir(parents=True, exist_ok=True)
    return deep


def test_web_tool_config_defaults():
    cfg = WebToolConfig(
        name="test tool",
        src_relative="some/path",
        output_subdir="tools/test",
    )
    assert cfg.dist_relative == "dist"
    assert cfg.skip_env_vars == ()


def test_web_tools_registry_has_expected_tools():
    output_dirs = [t.output_subdir for t in WEB_TOOLS]
    assert "tools/trace" in output_dirs
    assert "tools/playground" in output_dirs

    # output_subdir determines the published URL, so two tools must not share one
    assert len(output_dirs) == len(set(output_dirs))

    for tool in WEB_TOOLS:
        assert tool.name
        assert tool.src_relative
        assert tool.output_subdir.startswith("tools/")


def test_workspace_root_inputs_covers_every_file_except_buck(tmp_path: Path):
    # tools/web/ holds the Yarn workspace and nothing else, so every file there
    # is a build input and no allow-list is needed. Mirrors the glob in
    # tools/web/BUCK:web_workspace_srcs.
    workspace = tmp_path / "web"
    workspace.mkdir()
    root_files = {
        "package.json",
        "yarn.lock",
        "tsconfig.base.json",
        "vite.base.ts",
        "eslint.config.js",
        ".prettierrc",
        "web_tool.bzl",
    }
    for name in root_files:
        (workspace / name).write_text("x")
    (workspace / "BUCK").write_text("targets")
    member = workspace / "visualization_app"
    member.mkdir()
    (member / "package.json").write_text("x")

    assert {p.name for p in _workspace_root_inputs(workspace)} == root_files


def test_workspace_root_inputs_picks_up_new_config_without_a_code_change(
    tmp_path: Path,
):
    # The reason the workspace moved out of tools/: a new shared config file is
    # tracked automatically, with no list to update here or in Buck.
    workspace = tmp_path / "web"
    workspace.mkdir()
    (workspace / "package.json").write_text("x")
    assert {p.name for p in _workspace_root_inputs(workspace)} == {"package.json"}

    (workspace / "postcss.config.js").write_text("x")
    assert {p.name for p in _workspace_root_inputs(workspace)} == {
        "package.json",
        "postcss.config.js",
    }


def test_every_web_tool_sits_directly_under_the_workspace_root():
    # WebToolBuilder derives the workspace root as src_dir.parent, so every tool
    # must be a direct child of it.
    parents = {Path(tool.src_relative).parent for tool in WEB_TOOLS}
    assert parents == {Path("../../../tools/web")}


def test_dependency_packages_follows_declared_workspace_deps(tmp_path: Path):
    workspace = tmp_path / "web"
    _create_workspace(
        workspace,
        {
            "playground": {
                "name": "@openzl/playground",
                # A local package and a registry package, so we can check only
                # the local one is picked up.
                "dependencies": {"@openzl/web-common": "0.0.0", "react": "19.1.1"},
            },
            "visualizer": {"name": "@openzl/visualizer"},
            "web_common": {"name": "@openzl/web-common"},
        },
    )

    assert _dependency_packages(workspace / "playground", workspace) == [
        (workspace / "web_common").resolve()
    ]
    # A tool that declares nothing local depends on nothing local, even though
    # the shared package sits right next to it.
    assert _dependency_packages(workspace / "visualizer", workspace) == []


def test_dependency_packages_is_transitive(tmp_path: Path):
    workspace = tmp_path / "web"
    _create_workspace(
        workspace,
        {
            "playground": {
                "name": "@openzl/playground",
                "dependencies": {"@openzl/web-common": "0.0.0"},
            },
            "web_common": {
                "name": "@openzl/web-common",
                "dependencies": {"@openzl/web-icons": "0.0.0"},
            },
            "web_icons": {"name": "@openzl/web-icons"},
        },
    )

    assert _dependency_packages(workspace / "playground", workspace) == sorted(
        [(workspace / "web_common").resolve(), (workspace / "web_icons").resolve()]
    )


def test_dependency_packages_ignores_peer_dependencies(tmp_path: Path):
    # A peer dependency is supplied by the consumer, so it is not built from
    # these sources.
    workspace = tmp_path / "web"
    _create_workspace(
        workspace,
        {
            "playground": {
                "name": "@openzl/playground",
                "peerDependencies": {"@openzl/web-common": "0.0.0"},
            },
            "web_common": {"name": "@openzl/web-common"},
        },
    )

    assert _dependency_packages(workspace / "playground", workspace) == []


def test_dependency_packages_rejects_an_unregistered_tool(tmp_path: Path):
    # Forgetting to add the directory to `workspaces` must fail loudly rather
    # than silently building a tool Yarn does not know about.
    workspace = tmp_path / "web"
    _create_workspace(workspace, {"visualizer": {"name": "@openzl/visualizer"}})
    stray = workspace / "playground"
    stray.mkdir()
    (stray / "package.json").write_text(json.dumps({"name": "@openzl/playground"}))

    with pytest.raises(ValueError, match="not listed in the `workspaces` array"):
        _dependency_packages(stray, workspace)


def test_web_tools_registry_skip_env_vars_contain_generic():
    # static_docs_test relies on this flag to skip every tool at once
    for tool in WEB_TOOLS:
        assert "OPENZL_SKIP_WEB_TOOLS_BUILD" in tool.skip_env_vars


def test_stamp_compute_and_rebuild(tmp_path: Path):
    src_dir = tmp_path / "src"
    src_dir.mkdir()
    (src_dir / "file.txt").write_text("hello")

    stamp = Stamp(tmp_path / "stamp.txt", [src_dir], [])

    h1 = stamp.compute_stamp()
    assert h1.startswith("sha256=")
    # No stamp on disk yet
    assert stamp.needs_rebuild(h1) is True

    stamp.update_stamp(h1)
    assert stamp.needs_rebuild(h1) is False

    (src_dir / "file.txt").write_text("world")
    h2 = stamp.compute_stamp()
    assert h2 != h1
    assert stamp.needs_rebuild(h2) is True


def test_stamp_respects_excludes(tmp_path: Path):
    src_dir = tmp_path / "src"
    src_dir.mkdir()
    (src_dir / "keep.txt").write_text("keep")
    node_modules = src_dir / "node_modules"
    node_modules.mkdir()
    (node_modules / "ignore.txt").write_text("ignore")

    stamp = Stamp(tmp_path / "stamp.txt", [src_dir], [node_modules])
    h1 = stamp.compute_stamp()

    (node_modules / "ignore.txt").write_text("changed")
    assert stamp.compute_stamp() == h1

    (src_dir / "keep.txt").write_text("changed")
    assert stamp.compute_stamp() != h1


def test_web_tool_builder_fails_fast_on_missing_src(tmp_path: Path):
    docs_dir = _deep_docs_dir(tmp_path)
    site_dir = tmp_path / "site"
    site_dir.mkdir(exist_ok=True)

    config = FakeConfig(docs_dir, site_dir)
    missing_tool = WebToolConfig(
        name="missing",
        src_relative="../../../nonexistent_tool",
        output_subdir="tools/missing",
    )

    # A misconfigured registry entry must fail the build rather than silently
    # publish an empty directory
    with pytest.raises(AssertionError, match="source directory does not exist"):
        WebToolBuilder(config, str(tmp_path / "build"), missing_tool)


def test_web_tool_builder_build_successfully(tmp_path: Path):
    docs_dir = tmp_path / "docs"
    _create_workspace(docs_dir, {"my_tool_src": {"name": "@test/my-tool"}})
    site_dir = tmp_path / "site"
    site_dir.mkdir()
    build_dir = tmp_path / "build"

    tool_src = docs_dir / "my_tool_src"
    (tool_src / "index.html").write_text("<html>test</html>")

    dist_dir = tool_src / "dist"
    dist_dir.mkdir()
    (dist_dir / "index.html").write_text("<html>built</html>")

    config = FakeConfig(docs_dir, site_dir)
    tool = WebToolConfig(
        name="my tool",
        src_relative="my_tool_src",
        output_subdir="tools/my_tool",
        skip_env_vars=("OPENZL_SKIP_WEB_TOOLS_BUILD",),
    )

    builder = WebToolBuilder(config, str(build_dir), tool)

    # First build: no stamp on disk, so yarn runs (stubbed) and dist is copied
    with patch("mkdocs_openzl.plugin.check_call") as mock_call:
        builder.build()
        assert mock_call.call_count >= 1

    built_index = site_dir / "tools" / "my_tool" / "index.html"
    assert built_index.read_text() == "<html>built</html>"

    # Second build: sources unchanged and dist present, so yarn is skipped
    with patch("mkdocs_openzl.plugin.check_call") as mock_call:
        builder.build()
        mock_call.assert_not_called()


def test_web_tool_builder_rebuilds_when_a_declared_package_changes(tmp_path: Path):
    docs_dir = tmp_path / "docs"
    _create_workspace(
        docs_dir,
        {
            "tool_src": {
                "name": "@test/tool",
                "dependencies": {"@test/shared": "0.0.0"},
            },
            "shared": {"name": "@test/shared"},
        },
    )
    site_dir = tmp_path / "site"
    site_dir.mkdir()
    build_dir = tmp_path / "build"

    tool_src = docs_dir / "tool_src"
    (tool_src / "file.txt").write_text("tool")
    (tool_src / "dist").mkdir()
    (tool_src / "dist" / "index.html").write_text("built")

    shared_file = docs_dir / "shared" / "component.tsx"
    shared_file.write_text("first version")
    root_config = docs_dir / "vite.base.ts"
    root_config.write_text("export const shared = 1")

    tool = WebToolConfig(
        name="my tool",
        src_relative="tool_src",
        output_subdir="tools/my_tool",
    )
    builder = WebToolBuilder(FakeConfig(docs_dir, site_dir), str(build_dir), tool)

    with patch("mkdocs_openzl.plugin.check_call"):
        builder.build()

    shared_file.write_text("second version")
    with patch("mkdocs_openzl.plugin.check_call") as mock_call:
        builder.build()
        assert mock_call.call_count == 2

    # A workspace-root file is an input for every tool, with nothing declared.
    root_config.write_text("export const shared = 2")
    with patch("mkdocs_openzl.plugin.check_call") as mock_call:
        builder.build()
        assert mock_call.call_count == 2


def test_web_tool_builder_ignores_undeclared_sibling_packages(tmp_path: Path):
    docs_dir = tmp_path / "docs"
    _create_workspace(
        docs_dir,
        {
            "tool_src": {"name": "@test/tool"},
            "unrelated": {"name": "@test/unrelated"},
        },
    )
    site_dir = tmp_path / "site"
    site_dir.mkdir()
    build_dir = tmp_path / "build"

    tool_src = docs_dir / "tool_src"
    (tool_src / "file.txt").write_text("tool")
    (tool_src / "dist").mkdir()
    (tool_src / "dist" / "index.html").write_text("built")
    unrelated_file = docs_dir / "unrelated" / "component.tsx"
    unrelated_file.write_text("first version")

    tool = WebToolConfig(
        name="my tool",
        src_relative="tool_src",
        output_subdir="tools/my_tool",
    )
    builder = WebToolBuilder(FakeConfig(docs_dir, site_dir), str(build_dir), tool)

    with patch("mkdocs_openzl.plugin.check_call"):
        builder.build()

    unrelated_file.write_text("second version")
    with patch("mkdocs_openzl.plugin.check_call") as mock_call:
        builder.build()
        mock_call.assert_not_called()


def test_web_tool_builder_respects_skip_env(tmp_path: Path, monkeypatch):
    docs_dir = tmp_path / "docs"
    _create_workspace(docs_dir, {"tool_src": {"name": "@test/tool"}})
    site_dir = tmp_path / "site"
    site_dir.mkdir()
    build_dir = tmp_path / "build"

    tool_src = docs_dir / "tool_src"
    (tool_src / "file.txt").write_text("data")

    config = FakeConfig(docs_dir, site_dir)
    tool = WebToolConfig(
        name="skippable",
        src_relative="tool_src",
        output_subdir="tools/skippable",
        skip_env_vars=("OPENZL_SKIP_WEB_TOOLS_BUILD", "CUSTOM_SKIP"),
    )

    # The generic flag skips the build
    monkeypatch.setenv("OPENZL_SKIP_WEB_TOOLS_BUILD", "1")
    with patch("mkdocs_openzl.plugin.check_call") as mock_call:
        WebToolBuilder(config, str(build_dir), tool).build()
        mock_call.assert_not_called()
    assert not (site_dir / "tools" / "skippable").exists()

    # So does a tool-specific flag
    monkeypatch.delenv("OPENZL_SKIP_WEB_TOOLS_BUILD")
    monkeypatch.setenv("CUSTOM_SKIP", "1")
    with patch("mkdocs_openzl.plugin.check_call") as mock_call:
        WebToolBuilder(config, str(build_dir), tool).build()
        mock_call.assert_not_called()
    assert not (site_dir / "tools" / "skippable").exists()


def test_openzl_plugin_builds_all_tools(tmp_path: Path):
    docs_dir = _deep_docs_dir(tmp_path)
    site_dir = tmp_path / "site"
    site_dir.mkdir(exist_ok=True)
    build_dir = tmp_path / "build_openzl"
    build_dir.mkdir()

    # Materialize a fake Yarn workspace holding every registered tool. Sibling
    # package resolution is covered by the _dependency_packages tests; this one
    # only checks that each tool lands under its own output_subdir.
    workspace_root = (Path(docs_dir) / WEB_TOOLS[0].src_relative).parent.resolve()
    _create_workspace(
        workspace_root,
        {
            Path(tool.src_relative).name: {
                "name": f"@test/{Path(tool.src_relative).name}"
            }
            for tool in WEB_TOOLS
        },
    )

    for tool in WEB_TOOLS:
        src = (Path(docs_dir) / tool.src_relative).resolve()
        assert str(src).startswith(str(tmp_path)), (
            f"src for {tool.name} leaked to {src}, outside {tmp_path}"
        )
        (src / "src.txt").write_text(f"source for {tool.name}")
        dist = src / tool.dist_relative
        dist.mkdir(exist_ok=True)
        (dist / "index.html").write_text(f"<html>{tool.name}</html>")

    config = FakeConfig(docs_dir, site_dir)

    plugin = OpenZLPlugin()
    plugin_cfg = OpenZLConfig()
    plugin_cfg.load_dict({"build_directory": str(build_dir)})
    plugin.config = plugin_cfg

    with (
        patch.object(PythonBuilder, "build") as mock_py,
        patch("mkdocs_openzl.plugin.WebToolBuilder.build") as mock_web,
    ):
        plugin.on_pre_build(config)
        mock_py.assert_called_once()

        plugin.on_post_build(config)
        assert mock_web.call_count == len(WEB_TOOLS)

    # Now run the real builders with yarn stubbed out, and confirm each tool's
    # output actually lands under its own output_subdir
    with patch("mkdocs_openzl.plugin.check_call"):
        plugin.on_post_build(config)

    for tool in WEB_TOOLS:
        expected = site_dir / tool.output_subdir / "index.html"
        assert expected.exists(), f"Expected {expected} to exist for tool {tool.name}"


def test_adding_second_tool_only_requires_registry_entry(tmp_path: Path):
    """A second tool builds with no change to builder logic."""
    docs_dir = tmp_path / "docs"
    docs_dir.mkdir()
    site_dir = tmp_path / "site"
    site_dir.mkdir()
    build_dir = tmp_path / "build"

    _create_workspace(
        docs_dir,
        {
            "trace_src": {"name": "@test/trace"},
            "format_inspector_src": {"name": "@test/format-inspector"},
        },
    )
    for name in ("trace_src", "format_inspector_src"):
        src = docs_dir / name
        (src / "f.txt").write_text(name)
        (src / "dist").mkdir()
        (src / "dist" / "index.html").write_text(name)

    config = FakeConfig(docs_dir, site_dir)

    custom_tools = [
        WebToolConfig(
            name="trace visualizer",
            src_relative="trace_src",
            output_subdir="tools/trace",
        ),
        WebToolConfig(
            name="format inspector",
            src_relative="format_inspector_src",
            output_subdir="tools/format_inspector",
        ),
    ]

    for tool in custom_tools:
        with patch("mkdocs_openzl.plugin.check_call"):
            WebToolBuilder(config, str(build_dir), tool).build()

    # Each tool lands in its own subdirectory without clobbering the other
    assert (site_dir / "tools" / "trace" / "index.html").read_text() == "trace_src"
    assert (
        site_dir / "tools" / "format_inspector" / "index.html"
    ).read_text() == "format_inspector_src"
