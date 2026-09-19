# SPDX-License-Identifier: LGPL-2.1-or-later

from unittest.mock import MagicMock, patch

import pytest
from fub.osprofile import (
    Compiler,
    OsArch,
    OsName,
    OsRelease,
    PipPackageManager,
    UnknownArchException,
    UnknownOsException,
)


class TestOsName:
    @pytest.mark.parametrize(
        "name, expected",
        [
            ("fedora", OsName.FEDORA),
            ("centos", OsName.CENTOS),
            ("debian", OsName.DEBIAN),
            ("ubuntu", OsName.UBUNTU),
            ("arch", OsName.ARCH),
            ("darwin", OsName.DARWIN),
            ("freebsd", OsName.FREEBSD),
            ("nixos", OsName.NIXOS),
            ("rhel", OsName.CENTOS),  # custom mapping
        ],
        ids=[
            "fedora",
            "centos",
            "debian",
            "ubuntu",
            "arch",
            "darwin",
            "freebsd",
            "nixos",
            "rhel",
        ],
    )
    def test_from_string_known(self, name, expected):
        """Known OS names are parsed correctly."""
        assert OsName.from_string(name) == expected

    @pytest.mark.parametrize(
        "name",
        ["windows", "", "plan9", "haiku"],
        ids=["windows", "empty", "plan9", "haiku"],
    )
    def test_from_string_unknown_raises(self, name):
        """Unknown OS names raise UnknownOsException."""
        with pytest.raises(UnknownOsException):
            OsName.from_string(name)


class TestOsArch:
    @pytest.mark.parametrize(
        "arch, expected",
        [
            ("amd64", OsArch.AMD64),
            ("x86_64", OsArch.AMD64),
            ("arm", OsArch.ARM),
            ("armhf", OsArch.ARM),
            ("arm64", OsArch.ARM64),
            ("aarch64", OsArch.ARM64),
            ("i386", OsArch.I386),
            ("s390x", OsArch.S390X),
            ("ia64", OsArch.IA64),
        ],
        ids=[
            "amd64-debian",
            "x86_64-fedora",
            "arm-debian",
            "armhf-alternative",
            "arm64-debian",
            "aarch64-fedora",
            "i386",
            "s390x",
            "ia64",
        ],
    )
    def test_from_string_known(self, arch, expected):
        """Known arch names are parsed correctly."""
        assert OsArch.from_string(arch) == expected

    @pytest.mark.parametrize(
        "arch",
        ["riscv64", "unknown", ""],
        ids=["riscv64", "unknown", "empty"],
    )
    def test_from_string_unknown_raises(self, arch):
        """Unknown arch names raise UnknownArchException."""
        with pytest.raises(UnknownArchException):
            OsArch.from_string(arch)


class TestOsRelease:
    def test_detect_parses_file(self, tmp_path):
        """detect() reads the os-release file correctly."""
        os_release_content = (
            'NAME="Fedora Linux"\nID=fedora\nVERSION_ID="41"\nOTHER=stuff\n'
        )

        with patch("fub.osprofile.Path") as mock_path:
            mock_file = tmp_path / "os-release"
            mock_file.write_text(os_release_content)
            mock_path.return_value = mock_file

            result = OsRelease.detect()
            assert result.distro == "fedora"
            assert result.version == "41"

    def test_detect_missing_fields(self, tmp_path):
        """detect() raises FileNotFoundError when required fields are missing."""
        os_release_content = "NAME=something\n"

        with patch("fub.osprofile.Path") as mock_path:
            mock_file = tmp_path / "os-release"
            mock_file.write_text(os_release_content)
            mock_path.return_value = mock_file

            with pytest.raises(FileNotFoundError):
                OsRelease.detect()


class TestPackageManager:
    @pytest.mark.parametrize(
        "os_name, expected_cmd_parts",
        [
            (OsName.DARWIN, ["brew", "install"]),
            (OsName.DEBIAN, ["apt-get", "install", "-q"]),
            (OsName.UBUNTU, ["apt-get", "install", "-q"]),
            (OsName.FEDORA, ["dnf", "install"]),
            (OsName.CENTOS, ["dnf", "install"]),
            (OsName.ARCH, ["pacman", "-Syu", "--noconfirm", "--needed"]),
            (OsName.FREEBSD, ["pkg", "install"]),
        ],
        ids=[
            "darwin-brew",
            "debian-apt",
            "ubuntu-apt",
            "fedora-dnf",
            "centos-dnf",
            "arch-pacman",
            "freebsd-pkg",
        ],
    )
    def test_new_from_os_as_root(self, os_name, expected_cmd_parts):
        """As root, no sudo prefix is added."""
        with patch("fub.osprofile.os.geteuid", return_value=0):
            pm = os_name.package_manager()
            # Should not have sudo when running as root
            assert pm.cmd[0] != "sudo"
            for part in expected_cmd_parts:
                assert part in pm.cmd
            assert pm.cmd[-1] == "-y"

    @pytest.mark.parametrize(
        "os_name",
        [OsName.FEDORA, OsName.DEBIAN, OsName.UBUNTU],
        ids=["fedora", "debian", "ubuntu"],
    )
    def test_new_from_os_not_root_adds_sudo(self, os_name):
        """As non-root, sudo is prepended."""
        with patch("fub.osprofile.os.geteuid", return_value=1000):
            pm = os_name.package_manager()
            assert pm.cmd[0] == "sudo"

    def test_new_from_os_unknown_raises(self):
        """UnknownOsException is raised for unsupported OS."""
        with patch("fub.osprofile.os.geteuid", return_value=0):
            with pytest.raises(UnknownOsException, match="does not support"):
                OsName.NIXOS.package_manager()


class TestPipPackageManager:
    def test_new_creates_pip_manager(self, tmp_path):
        """new() returns a PipPackageManager with correct env."""
        python = tmp_path / "bin" / "python"
        python.parent.mkdir(parents=True)
        python.touch()

        pm = PipPackageManager.new(python)
        assert python in pm.cmd
        assert "pip" in " ".join(str(c) for c in pm.cmd)
        assert pm.env.get("PIP_BREAK_SYSTEM_PACKAGES") == "1"

    def test_install_package_already_installed(self, tmp_path):
        """install_package returns None when package is already installed."""
        python = tmp_path / "bin" / "python"
        python.parent.mkdir(parents=True)
        python.touch()

        pm = PipPackageManager.new(python)

        mock_cmd = MagicMock()
        mock_cmd.success = True
        mock_cmd.stdout = "1.2.3\n"

        with patch("fub.osprofile.RunCmd", return_value=mock_cmd):
            result = pm.install_package("markdown", version=(1, 0, 0))
            assert result is None

    def test_install_package_needs_upgrade(self, tmp_path):
        """install_package installs when version is too old."""
        python = tmp_path / "bin" / "python"
        python.parent.mkdir(parents=True)
        python.touch()

        pm = PipPackageManager.new(python)

        check_cmd = MagicMock()
        check_cmd.success = True
        check_cmd.stdout = "1.0.0\n"

        install_cmd = MagicMock()
        install_cmd.success = True

        with patch("fub.osprofile.RunCmd", side_effect=[check_cmd, install_cmd]):
            result = pm.install_package("markdown", version=(2, 0, 0))
            assert result is not None
            assert result == install_cmd

    def test_install_package_not_found(self, tmp_path):
        """install_package installs when package is not found."""
        python = tmp_path / "bin" / "python"
        python.parent.mkdir(parents=True)
        python.touch()

        pm = PipPackageManager.new(python)

        check_cmd = MagicMock()
        check_cmd.success = False

        install_cmd = MagicMock()
        install_cmd.success = True

        with patch("fub.osprofile.RunCmd", side_effect=[check_cmd, install_cmd]):
            result = pm.install_package("markdown")
            assert result is not None
            assert result == install_cmd


class TestCompiler:
    def test_machine(self):
        """machine property returns the compiler's target machine."""
        compiler = Compiler(prog="cc")
        mock_cmd = MagicMock()
        mock_cmd.success = True
        mock_cmd.stdout = "x86_64-redhat-linux-gnu\n"
        with patch("fub.osprofile.RunCmd", return_value=mock_cmd):
            assert compiler.machine == "x86_64-redhat-linux-gnu"

    def test_machine_failure(self):
        """machine returns empty string when cc fails."""
        compiler = Compiler(prog="cc")
        mock_cmd = MagicMock()
        mock_cmd.success = False
        mock_cmd.stdout = ""
        with patch("fub.osprofile.RunCmd", return_value=mock_cmd):
            assert compiler.machine == ""

    def test_get_cc(self):
        """get_cc returns a Compiler with the given name."""
        c = Compiler.get_cc("gcc")
        assert c.prog == "gcc"

    def test_get_cc_default(self):
        """get_cc defaults to 'cc'."""
        c = Compiler.get_cc()
        assert c.prog == "cc"
