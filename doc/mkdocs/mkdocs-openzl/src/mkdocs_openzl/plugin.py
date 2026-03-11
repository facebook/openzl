# Copyright (c) Meta Platforms, Inc. and affiliates.

import hashlib
import os
import shlex
import shutil
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


class StreamdumpBuilder:
    def __init__(self, config: MkDocsConfig, build_directory: str):
        self._config = config
        self._src_dir = Path(config.docs_dir) / "../../../tools/visualization_app"
        assert self._src_dir.exists()
        self._build_dir = Path(build_directory) / "tools" / "trace"
        self._skip_build = os.getenv("OPENZL_SKIP_VISUALIZER_BUILD", False)
        self._stamp = Stamp(
            self._build_dir / "stamp.txt",
            [self._src_dir],
            [
                self._src_dir / "node_modules",
                self._src_dir / "dist",
            ],
        )

    def build(self) -> None:
        """
        Build the visualization app
        """
        if self._skip_build:
            print(
                "Skipping trace visualizer build (OPENZL_SKIP_VISUALIZER_BUILD is set)"
            )
            return

        stamp = self._stamp.compute_stamp()

        if self._stamp.needs_rebuild(stamp) or not (self._src_dir / "dist").exists():
            print("Building trace visualizer...")
            check_call(["yarn"], cwd=self._src_dir)
            check_call(["yarn", "build"], cwd=self._src_dir)
        else:
            print("Skipping trace visualizer build because the sources haven't changed")

        # copy the visualization app to the docs directory
        site_dir = Path(self._config.site_dir) / "tools" / "trace"
        shutil.rmtree(site_dir, ignore_errors=True)
        shutil.copytree(
            self._src_dir / "dist",
            site_dir,
        )

        self._stamp.update_stamp(stamp)


class PythonBuilder:
    def __init__(self, config: MkDocsConfig, build_directory: str):
        self._config = config
        self._src_dir = Path(config.docs_dir) / "../../../py"
        self._build_dir = Path(build_directory) / "py"
        self._use_system_python_extension = os.getenv(
            "OPENZL_USE_SYSTEM_PYTHON_EXTENSION", False
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
        StreamdumpBuilder(config, self.config.build_directory).build()
