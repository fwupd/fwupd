# SPDX-License-Identifier: LGPL-2.1-or-later

import time
from pathlib import Path

import pytest
from fub.directories import Directories


class TestFindRepoRoot:
    def test_found(self, tmp_path):
        """find_repo_root finds a parent containing meson_options.txt."""
        # Create a fake repo structure
        repo = tmp_path / "project"
        repo.mkdir()
        (repo / "meson_options.txt").touch()
        subdir = repo / "contrib" / "fub"
        subdir.mkdir(parents=True)
        fake_file = subdir / "directories.py"
        fake_file.touch()

        import fub.directories as mod

        orig = mod.__file__
        try:
            mod.__file__ = str(fake_file)
            result = Directories.find_repo_root()
            assert result == repo
        finally:
            mod.__file__ = orig

    def test_not_found(self, tmp_path):
        """find_repo_root raises FileNotFoundError when no marker exists."""
        # A directory with no meson_options.txt anywhere up the tree
        subdir = tmp_path / "a" / "b" / "c"
        subdir.mkdir(parents=True)
        fake_file = subdir / "directories.py"
        fake_file.touch()

        import fub.directories as mod

        orig = mod.__file__
        try:
            mod.__file__ = str(fake_file)
            with pytest.raises(
                FileNotFoundError, match="Unable to find repository root"
            ):
                Directories.find_repo_root()
        finally:
            mod.__file__ = orig


class TestFindMostRecentBuildRoot:
    def test_finds_most_recent(self, tmp_path):
        """find_most_recent_build_root returns the directory with the newest marker."""
        import fub.directories as mod

        repo = tmp_path / "project"
        repo.mkdir()
        (repo / "meson_options.txt").touch()

        # Create two build_roots with marker files
        build_root1 = repo / "builddir1"
        build_root1.mkdir()
        marker1 = build_root1 / Directories.MARKERFILE
        marker1.touch()

        # Ensure different mtime
        time.sleep(0.05)

        build_root2 = repo / "builddir2"
        build_root2.mkdir()
        marker2 = build_root2 / Directories.MARKERFILE
        marker2.touch()

        orig = mod.__file__
        try:
            mod.__file__ = str(repo / "directories.py")
            result = Directories.find_most_recent_build_root()
            assert result == build_root2
        finally:
            mod.__file__ = orig

    def test_none_when_no_markers(self, tmp_path):
        """find_most_recent_build_root returns None when no markers exist."""
        import fub.directories as mod

        repo = tmp_path / "project"
        repo.mkdir()
        (repo / "meson_options.txt").touch()

        orig = mod.__file__
        try:
            mod.__file__ = str(repo / "directories.py")
            result = Directories.find_most_recent_build_root()
            assert result is None
        finally:
            mod.__file__ = orig


class TestDirectoriesPaths:
    def test_repository_root_relative(self, tmp_path):
        """repository_root() returns the path as-is when absolute=False."""
        d = Directories(_repository_root=tmp_path, _build_root=tmp_path / "build")
        assert d.repository_root() == tmp_path

    def test_repository_root_absolute(self, tmp_path):
        """repository_root(absolute=True) returns an absolute path."""
        rel = Path("relative/path")
        d = Directories(_repository_root=rel, _build_root=Path("build"))
        result = d.repository_root(absolute=True)
        assert result.is_absolute()

    def test_build_root_relative(self, tmp_path):
        """build_root() returns the build root path when absolute=False."""
        build = tmp_path / "mybuild"
        d = Directories(_repository_root=tmp_path, _build_root=build)
        assert d.build_root() == build

    def test_build_root_absolute(self, tmp_path):
        """build_root(absolute=True) returns repo_root / build_root."""
        d = Directories(
            _repository_root=tmp_path,
            _build_root=Path("mybuild"),
        )
        result = d.build_root(absolute=True)
        assert result == tmp_path / "mybuild"

    def test_builddir(self, tmp_path):
        """builddir() returns build_root / 'build'."""
        build = tmp_path / "mybuild"
        d = Directories(_repository_root=tmp_path, _build_root=build)
        assert d.builddir() == build / "build"

    def test_distdir(self, tmp_path):
        """distdir() returns build_root / 'dist'."""
        build = tmp_path / "mybuild"
        d = Directories(_repository_root=tmp_path, _build_root=build)
        assert d.distdir() == build / "dist"


class TestBuildRootState:
    @pytest.mark.parametrize("is_initialized", [True, False])
    def test_is_initialized_true(self, tmp_path, is_initialized):
        """build_root_is_initialized is True when marker file exists."""
        build = tmp_path / "mybuild"
        build.mkdir()
        if is_initialized:
            (build / Directories.MARKERFILE).touch()
        d = Directories(_repository_root=tmp_path, _build_root=build)
        assert d.build_root_is_initialized == is_initialized

    def test_mark_as_ready_creates_marker(self, tmp_path):
        """build_root_mark_as_ready creates the marker file."""
        build = tmp_path / "mybuild"
        build.mkdir()
        d = Directories(_repository_root=tmp_path, _build_root=build)
        assert not (build / Directories.MARKERFILE).exists()
        d.build_root_mark_as_ready()
        assert (build / Directories.MARKERFILE).exists()

        # build_root_mark_as_ready is safe to call multiple times
        d.build_root_mark_as_ready()
        d.build_root_mark_as_ready()  # should not raise anything
        assert (build / Directories.MARKERFILE).exists()


class TestPopulate:
    def test_populate_with_explicit_builddir(self, tmp_path):
        """populate() uses the provided builddir."""
        import fub.directories as mod

        repo = tmp_path / "project"
        repo.mkdir()
        (repo / "meson_options.txt").touch()
        builddir = tmp_path / "custom-build"

        orig = mod.__file__
        try:
            mod.__file__ = str(repo / "directories.py")
            d = Directories.populate(builddir=builddir)
            assert d._build_root == builddir
            assert d._repository_root == repo
        finally:
            mod.__file__ = orig

    def test_populate_defaults_to_builddir(self, tmp_path):
        """populate() defaults to repo_root/builddir when no build_root found."""
        import fub.directories as mod

        repo = tmp_path / "project"
        repo.mkdir()
        (repo / "meson_options.txt").touch()

        orig = mod.__file__
        try:
            mod.__file__ = str(repo / "directories.py")
            d = Directories.populate()
            assert d._build_root == repo / "builddir"
        finally:
            mod.__file__ = orig
