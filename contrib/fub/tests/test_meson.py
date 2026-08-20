# SPDX-License-Identifier: LGPL-2.1-or-later

from pathlib import Path
from unittest.mock import patch

import pytest
from fub.meson import Meson, MesonError, MesonVersion


class TestMesonVersionFromString:
    @pytest.mark.parametrize(
        "ver, expected",
        [
            ("1.2.3", MesonVersion(1, 2, 3, 0, 0)),
            ("0.0.0", MesonVersion(0, 0, 0, 0, 0)),
            ("10.20.30", MesonVersion(10, 20, 30, 0, 0)),
            ("1.2.3rc1", MesonVersion(1, 2, 3, -1, 1)),
            ("1.2.3.rc1", MesonVersion(1, 2, 3, -1, 1)),
            ("1.2.3.rc99", MesonVersion(1, 2, 3, -1, 99)),
            ("0.62.0", MesonVersion(0, 62, 0, 0, 0)),
            ("1.0.0rc1", MesonVersion(1, 0, 0, -1, 1)),
        ],
        ids=[
            "simple",
            "zeros",
            "large-numbers",
            "rc-no-dot",
            "rc-with-dot",
            "rc-large-number",
            "typical-meson-version",
            "rc-with-zero-minor-micro",
        ],
    )
    def test_valid_versions(self, ver, expected):
        assert MesonVersion.from_string(ver) == expected

    @pytest.mark.parametrize(
        "ver",
        [
            "1.2",
            "1",
            "abc",
            "",
            "1.2.3.4",
            "1.2.3rc",
            "1.2.3.rc",
            "v1.2.3",
            "1.2.3-rc1",
            "1.2.3beta1",
        ],
        ids=[
            "two-components",
            "one-component",
            "letters",
            "empty",
            "four-components",
            "rc-no-number",
            "dot-rc-no-number",
            "v-prefix",
            "dash-rc",
            "beta-suffix",
        ],
    )
    def test_invalid_versions(self, ver):
        with pytest.raises(MesonError, match="Unknown meson version format"):
            MesonVersion.from_string(ver)


class TestMesonVersionStr:
    @pytest.mark.parametrize(
        "version, expected",
        [
            (MesonVersion(1, 2, 3, 0, 0), "1.2.3"),
            (MesonVersion(0, 0, 0, 0, 0), "0.0.0"),
            (MesonVersion(10, 20, 30, 0, 0), "10.20.30"),
            (MesonVersion(1, 2, 3, -1, 1), "1.2.3rc1"),
            (MesonVersion(1, 2, 3, -1, 99), "1.2.3rc99"),
        ],
        ids=[
            "simple",
            "zeros",
            "large-numbers",
            "rc",
            "rc-large-number",
        ],
    )
    def test_str(self, version, expected):
        assert str(version) == expected

    @pytest.mark.parametrize(
        "ver",
        [
            "1.2.3",
            "0.62.0",
            "1.2.3rc1",
            "1.2.3.rc1",
        ],
        ids=[
            "simple",
            "typical",
            "rc-no-dot",
            "rc-with-dot",
        ],
    )
    def test_roundtrip(self, ver):
        """str(from_string(ver)) should reproduce the canonical form."""
        v = MesonVersion.from_string(ver)
        # rc with dot normalizes to rc without dot
        expected = ver.replace(".rc", "rc")
        assert str(v) == expected


class TestMesonVersionComparison:
    @pytest.mark.parametrize(
        "lower, higher",
        [
            ("0.1.0", "0.2.0"),
            ("0.1.0", "1.0.0"),
            ("1.2.3", "1.2.4"),
            ("1.2.3", "1.3.0"),
            ("1.2.3", "2.0.0"),
            ("1.2.3rc1", "1.2.3"),
            ("1.2.3rc1", "1.2.3rc2"),
            ("0.62.0", "1.0.0"),
        ],
        ids=[
            "minor-bump",
            "major-bump",
            "micro-bump",
            "minor-over-micro",
            "major-over-minor",
            "rc-less-than-release",
            "rc1-less-than-rc2",
            "old-vs-new-major",
        ],
    )
    def test_less_than(self, lower, higher):
        assert MesonVersion.from_string(lower) < MesonVersion.from_string(higher)

    @pytest.mark.parametrize(
        "a, b",
        [
            ("1.2.3", "1.2.3"),
            ("0.0.0", "0.0.0"),
            ("1.2.3rc1", "1.2.3rc1"),
            # dot-rc and no-dot-rc are the same version
            ("1.2.3.rc1", "1.2.3rc1"),
        ],
        ids=[
            "same-release",
            "zeros",
            "same-rc",
            "dot-rc-equals-no-dot-rc",
        ],
    )
    def test_equal(self, a, b):
        assert MesonVersion.from_string(a) == MesonVersion.from_string(b)

    @pytest.mark.parametrize(
        "a, b",
        [
            ("1.2.3", "1.2.4"),
            ("1.2.3rc1", "1.2.3"),
            ("1.0.0", "2.0.0"),
        ],
        ids=[
            "different-micro",
            "rc-vs-release",
            "different-major",
        ],
    )
    def test_not_equal(self, a, b):
        assert MesonVersion.from_string(a) != MesonVersion.from_string(b)


class TestMesonNeedsSetup:
    @pytest.mark.parametrize(
        "ninja_exists, expected",
        [
            (True, False),
            (False, True),
        ],
        ids=[
            "build-ninja-exists",
            "build-ninja-missing",
        ],
    )
    def test_needs_setup(self, tmp_path, ninja_exists, expected):
        builddir = tmp_path / "build"
        builddir.mkdir()
        if ninja_exists:
            (builddir / "build.ninja").touch()

        meson = Meson(builddir=builddir, meson_args=[])
        assert meson.needs_setup is expected


class TestMesonSetup:
    @pytest.mark.parametrize(
        "meson_args, prefix, cwd, capture_logs, expected_cmd_parts, expected_kwargs",
        [
            (
                ["-Dfoo=bar"],
                None,
                None,
                False,
                ["meson", "setup", "-Dfoo=bar"],
                {"capture": False},
            ),
            (
                ["-Dfoo=bar"],
                Path("/usr/local"),
                None,
                False,
                ["meson", "setup", "--prefix=/usr/local", "-Dfoo=bar"],
                {"capture": False},
            ),
            (
                ["-Dfoo=bar"],
                None,
                Path("/some/srcdir"),
                False,
                ["meson", "setup", "-Dfoo=bar"],
                {"capture": False, "cwd": "/some/srcdir"},
            ),
            (
                ["-Dfoo=bar"],
                None,
                None,
                True,
                ["meson", "setup", "-Dfoo=bar"],
                {"capture": True},
            ),
            (
                [],
                Path("/opt"),
                Path("/src"),
                True,
                ["meson", "setup", "--prefix=/opt"],
                {"capture": True, "cwd": "/src"},
            ),
        ],
        ids=[
            "basic-args",
            "with-prefix",
            "with-cwd",
            "with-capture",
            "all-options",
        ],
    )
    def test_setup(
        self,
        tmp_path,
        meson_args,
        prefix,
        cwd,
        capture_logs,
        expected_cmd_parts,
        expected_kwargs,
    ):
        builddir = tmp_path / "build"
        meson = Meson(
            builddir=builddir,
            meson_args=meson_args,
            prefix=prefix,
            cwd=cwd,
            capture_logs=capture_logs,
        )

        with patch("fub.meson.RunCmd") as mock_runcmd:
            meson.setup()

            args, kwargs = mock_runcmd.call_args
            cmd = args[0]
            # The builddir is always included in the command
            assert cmd[0:2] == ["meson", "setup"]
            assert str(builddir) in cmd
            for part in expected_cmd_parts:
                assert part in cmd
            for k, v in expected_kwargs.items():
                assert kwargs[k] == str(v) if k == "cwd" else kwargs[k] == v


class TestMesonBuild:
    def test_build(self, tmp_path):
        builddir = tmp_path / "build"
        meson = Meson(builddir=builddir, meson_args=[], capture_logs=True)

        with patch("fub.meson.RunCmd") as mock_runcmd:
            meson.build()
            mock_runcmd.assert_called_once_with(
                ["meson", "compile", "-C", builddir], capture=True
            )

    @pytest.mark.parametrize(
        "capture_logs",
        [True, False],
        ids=["capture-on", "capture-off"],
    )
    def test_build_capture_flag(self, tmp_path, capture_logs):
        builddir = tmp_path / "build"
        meson = Meson(builddir=builddir, meson_args=[], capture_logs=capture_logs)

        with patch("fub.meson.RunCmd") as mock_runcmd:
            meson.build()
            mock_runcmd.assert_called_once_with(
                ["meson", "compile", "-C", builddir], capture=capture_logs
            )


class TestMesonInstall:
    @pytest.mark.parametrize(
        "destdir, capture_logs, expected_kwargs",
        [
            (None, False, {"capture": False}),
            (None, True, {"capture": True}),
            ("/tmp/dest", False, {"capture": False, "env": {"DESTDIR": "/tmp/dest"}}),
            ("/tmp/dest", True, {"capture": True, "env": {"DESTDIR": "/tmp/dest"}}),
        ],
        ids=[
            "no-destdir-no-capture",
            "no-destdir-capture",
            "with-destdir-no-capture",
            "with-destdir-capture",
        ],
    )
    def test_install(self, tmp_path, destdir, capture_logs, expected_kwargs):
        builddir = tmp_path / "build"
        meson = Meson(builddir=builddir, meson_args=[], capture_logs=capture_logs)

        with patch("fub.meson.RunCmd") as mock_runcmd:
            meson.install(destdir=destdir)
            mock_runcmd.assert_called_once_with(
                ["meson", "install", "-C", builddir], **expected_kwargs
            )


class TestMesonGetMinimumMesonVersion:
    @pytest.mark.parametrize(
        "file_content, expected",
        [
            (
                "project('fwupd', meson_version: '>= 0.62.0')\n",
                MesonVersion(0, 62, 0, 0, 0),
            ),
            (
                "project('foo',\n  meson_version: '>= 1.2.3',\n)\n",
                MesonVersion(1, 2, 3, 0, 0),
            ),
            (
                "# comment\nproject('bar', meson_version : '>= 0.59.0')\nsubdir('src')\n",
                MesonVersion(0, 59, 0, 0, 0),
            ),
            (
                "stuff\nmeson_version: '>= 10.20.30'\nmore stuff\n",
                MesonVersion(10, 20, 30, 0, 0),
            ),
        ],
        ids=[
            "standard-project-line",
            "multiline-project",
            "with-comments-and-trailing",
            "meson-version-in-middle",
        ],
    )
    def test_found(self, tmp_path, file_content, expected):
        meson_build = tmp_path / "meson.build"
        meson_build.write_text(file_content)
        result = Meson.get_minimum_meson_version(meson_build)
        assert result == expected

    @pytest.mark.parametrize(
        "file_content",
        [
            "",
            "project('foo')\n",
            "# meson_version is not set here\nsubdir('src')\n",
        ],
        ids=[
            "empty-file",
            "no-meson-version",
            "comment-only-mention",
        ],
    )
    def test_not_found(self, tmp_path, file_content):
        meson_build = tmp_path / "meson.build"
        meson_build.write_text(file_content)
        with pytest.raises(
            Exception, match="does not contain a required meson version"
        ):
            Meson.get_minimum_meson_version(meson_build)
