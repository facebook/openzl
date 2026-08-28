# Copyright (c) Meta Platforms, Inc. and affiliates.

import hashlib
import os
import shlex
import shutil
from dataclasses import dataclass
from pathlib import Path
from subprocess import check_call
from typing import List, Optional

import mkdocs
from mkdocs.config.defaults import MkDocsConfig


class OpenZLConfig(mkdocs.config.base.Config):
    build_directory = mkdocs.config.config_options.Type(str)


class Stamp:
    """
    Helper class to determine whether sources have changed and the output needs to be recomputed.
    """

    def __init__(self, stamp_file: Path, sources: List[Path], excludes: List[Path]):
        self._stamp_file = Path(stamp_file)
        self._sources = [Path(d).absolute() for d in sources]
        self._excludes = {Path(d).absolute() for d in excludes}
        self._stamp = None

    def _read_stamp(self) -> Optional[str]:
        if not self._stamp_file.exists():
            return None
        with open(self._stamp_file, "r") as f:
            return f.read()

    def _write_stamp(self, stamp) -> None:
        os.makedirs(self._stamp_file.parent, exist_ok=True)
        with open(self._stamp_file, "w") as f:
            f.write(stamp)

    def compute_stamp(self) -> str:
        h = hashlib.sha256()

        for source_dir in self._sources:
            if not source_dir.exists():
                continue
            for root, dirs, files in os.walk(source_dir, topdown=True):
                for d in list(dirs):
                    path = Path(root) / d
                    if path in self._excludes:
                        dirs.remove(d)
                for f in files:
                    path = Path(root) / f
                    if path in self._excludes:
                        continue
                    with open(path, "rb") as f:
                        h.update(f.read())

        return f"sha256={h.hexdigest()}"

    def needs_rebuild(self, stamp: str) -> bool:
        """
        Returns true if the output needs to be recomputed
        """
        assert stamp is not None
        old_stamp = self._read_stamp()
        return stamp != old_stamp

    def update_stamp(self, stamp: str):
        """
        Sets the stamp file
        """
        self._write_stamp(stamp)


@dataclass(frozen=True)
class WebToolConfig:
    """
    Configuration for a single web tool embedded in the docs site.

    Attributes:
        name: Human readable name used in logs.
        src_relative: Path to the tool source directory, relative to docs_dir
                      (e.g. "../../../tools/visualization_app").
        output_subdir: Subdirectory inside site_dir where the built assets are copied
                       (e.g. "tools/trace"). This must match the Vite `base` option
                       for the tool (e.g. base: "/tools/trace").
        dist_relative: Relative path from src dir to built output (default "dist").
        skip_env_vars: Env vars that, when set to "1", skip this tool's build.
                       OPENZL_SKIP_WEB_TOOLS_BUILD skips every tool; a tool may
                       list additional vars to skip only itself.
    """

    name: str
    src_relative: str
    output_subdir: str
    dist_relative: str = "dist"
    skip_env_vars: tuple[str, ...] = ()


# Registry of all web tools that should be built and copied into the site.
# To add a new tool:
#   1. Add a new WebToolConfig entry here pointing at its source dir and output subdir.
#   2. Set its Vite config `base` to "/<output_subdir>" (e.g. "/tools/my_tool").
#   3. Add a navigation entry in mkdocs.yml under Tools.
WEB_TOOLS: list[WebToolConfig] = [
    WebToolConfig(
        name="trace visualizer",
        src_relative="../../../tools/visualization_app",
        output_subdir="tools/trace",
        skip_env_vars=("OPENZL_SKIP_WEB_TOOLS_BUILD",),
    ),
]


class WebToolBuilder:
    """
    Builds one web tool and copies its output into the docs site.
    """

    def __init__(self, config: MkDocsConfig, build_directory: str, tool: WebToolConfig):
        self._config = config
        self._tool = tool
        self._src_dir = Path(config.docs_dir) / tool.src_relative
        assert self._src_dir.exists(), (
            f"Web tool '{tool.name}' source directory does not exist: {self._src_dir} "
            f"(configured as '{tool.src_relative}' relative to docs_dir). "
            f"Check WEB_TOOLS registry and that the directory is present."
        )
        # Build dir is where we store the stamp file; mirrors the output_subdir
        # structure to keep stamps per-tool isolated.
        self._build_dir = Path(build_directory) / tool.output_subdir
        self._skip_reason: str | None = None
        for env_var in tool.skip_env_vars:
            if os.getenv(env_var, "") == "1":
                self._skip_reason = f"{env_var} is set"
                break
        self._stamp = Stamp(
            self._build_dir / "stamp.txt",
            [self._src_dir],
            [
                self._src_dir / "node_modules",
                self._src_dir / tool.dist_relative,
            ],
        )

    def build(self) -> None:
        if self._skip_reason is not None:
            print(f"Skipping {self._tool.name} build ({self._skip_reason})")
            return

        stamp = self._stamp.compute_stamp()
        dist_dir = self._src_dir / self._tool.dist_relative

        if self._stamp.needs_rebuild(stamp) or not dist_dir.exists():
            print(f"Building {self._tool.name}...")
            check_call(["yarn"], cwd=self._src_dir)
            check_call(["yarn", "build"], cwd=self._src_dir)
        else:
            print(
                f"Skipping {self._tool.name} build because the sources haven't changed"
            )

        site_dir = Path(self._config.site_dir) / self._tool.output_subdir
        shutil.rmtree(site_dir, ignore_errors=True)
        shutil.copytree(
            dist_dir,
            site_dir,
        )

        self._stamp.update_stamp(stamp)


class PythonBuilder:
    def __init__(self, config: MkDocsConfig, build_directory: str):
        self._config = config
        self._src_dir = Path(config.docs_dir) / "../../../py"
        self._build_dir = Path(build_directory) / "py"
        self._use_system_python_extension = (
            os.getenv("OPENZL_USE_SYSTEM_PYTHON_EXTENSION", "") == "1"
        )
        self._stamp = Stamp(
            self._build_dir / "stamp.txt",
            [self._src_dir],
            [],
        )

    def _build_with_pip(self, pkg_dir: Path) -> None:
        pip = shlex.split(os.getenv("OPENZL_PIP", "pip"))
        check_call(
            pip + ["install", ".", "--target", pkg_dir.absolute()],
            cwd=self._src_dir,
        )

    def build(self) -> None:
        """
        Build the python docs
        """
        pkg_dir = self._build_dir / "site-packages"

        if self._use_system_python_extension:
            print("Using system Python extension")
            shutil.rmtree(pkg_dir, ignore_errors=True)
            return

        stamp = self._stamp.compute_stamp()

        pkg_dir = self._build_dir / "site-packages"
        if self._stamp.needs_rebuild(stamp) or not pkg_dir.exists():
            shutil.rmtree(pkg_dir, ignore_errors=True)
            print("Building python package with pip...")
            self._build_with_pip(pkg_dir)
            print("Built python package")
        else:
            print("Skipping python package build: Sources haven't changed")

        self._stamp.update_stamp(stamp)


class OpenZLPlugin(mkdocs.plugins.BasePlugin[OpenZLConfig]):
    def on_pre_build(self, config: MkDocsConfig) -> None:
        PythonBuilder(config, self.config.build_directory).build()

    def on_post_build(self, config: MkDocsConfig) -> None:
        for tool in WEB_TOOLS:
            WebToolBuilder(config, self.config.build_directory, tool).build()
