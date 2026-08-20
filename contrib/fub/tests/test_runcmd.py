# SPDX-License-Identifier: LGPL-2.1-or-later

import subprocess
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest
from fub.runcmd import RunCmd, SudoMissing


class TestRunCmdBasic:
    def test_captures_stdout(self):
        """RunCmd captures stdout by default."""
        cmd = RunCmd(["echo", "hello"])
        assert cmd.stdout.strip() == "hello"

    def test_captures_stderr(self):
        """RunCmd captures stderr by default."""
        cmd = RunCmd(["sh", "-c", "echo oops >&2"])
        assert "oops" in cmd.stderr

    def test_success_on_zero_exit(self):
        """success is True when the command exits with 0."""
        cmd = RunCmd(["true"])
        assert cmd.success is True
        assert cmd.returncode == 0

    def test_failure_on_nonzero_exit(self):
        """success is False when the command exits non-zero."""
        cmd = RunCmd(["false"])
        assert cmd.success is False
        assert cmd.returncode != 0

    def test_check_raises_on_failure(self):
        """check=True raises CalledProcessError on non-zero exit."""
        with pytest.raises(subprocess.CalledProcessError):
            RunCmd(["false"], check=True)

    def test_no_capture(self):
        """capture=False does not set stdout/stderr on the process."""
        cmd = RunCmd(["echo", "hello"], capture=False)
        # When not capturing, subprocess.run doesn't capture stdout
        assert cmd.p.stdout is None

    def test_returncode(self):
        """returncode reflects the actual exit code."""
        cmd = RunCmd(["sh", "-c", "exit 42"])
        assert cmd.returncode == 42


class TestRunCmdArgs:
    def test_args_coerced_to_strings(self):
        """Non-string args (Path, int) are coerced to strings."""
        cmd = RunCmd(["echo", Path("/tmp"), 42])
        assert cmd.success is True
        assert "/tmp" in cmd.stdout
        assert "42" in cmd.stdout

    def test_cwd_coerced_to_string(self):
        """A Path passed as cwd is converted to a string."""
        cmd = RunCmd(["pwd"], cwd=Path("/tmp"))
        # /tmp may resolve to a symlink target, so just check it worked
        assert cmd.success is True


class TestRunCmdSudo:
    def test_as_sudo_prepends_sudo(self):
        """as_sudo=True prepends the sudo binary to the command."""
        with patch("fub.runcmd.shutil.which", return_value="/usr/bin/sudo"):
            with patch("fub.runcmd.subprocess.run") as mock_run:
                mock_run.return_value = MagicMock(returncode=0, stdout="", stderr="")
                RunCmd(["ls"], as_sudo=True)
                args = mock_run.call_args[0][0]
                assert args[0] == "/usr/bin/sudo"
                assert "ls" in args

    def test_as_sudo_with_env(self):
        """sudo_env inserts KEY=VALUE pairs after the sudo binary."""
        with patch("fub.runcmd.shutil.which", return_value="/usr/bin/sudo"):
            with patch("fub.runcmd.subprocess.run") as mock_run:
                mock_run.return_value = MagicMock(returncode=0, stdout="", stderr="")
                RunCmd(
                    ["ls"],
                    as_sudo=True,
                    sudo_env={"FOO": "bar", "BAZ": "qux"},
                )
                args = mock_run.call_args[0][0]
                assert args[0] == "/usr/bin/sudo"
                assert "FOO=bar" in args
                assert "BAZ=qux" in args
                assert args[-1] == "ls"

    def test_as_sudo_missing_raises(self):
        """SudoMissing is raised when sudo is not found."""
        with patch("fub.runcmd.shutil.which", return_value=None):
            with pytest.raises(SudoMissing):
                RunCmd(["ls"], as_sudo=True)


class TestRunCmdDaemon:
    def test_context_manager_kills_daemon(self):
        """Using RunCmd as a context manager in daemon mode kills the process on exit."""
        with RunCmd(["sleep", "60"], daemon=True) as cmd:
            assert cmd.daemon_process is not None
            assert cmd.daemon_process.poll() is None  # still running
        # After exiting the context, the process should be terminated
        assert cmd.daemon_process.poll() is not None

    def test_daemon_not_set_for_normal(self):
        """daemon_process is None for non-daemon commands."""
        cmd = RunCmd(["true"])
        assert cmd.daemon_process is None


class TestRunCmdLogging:
    def test_log_stdout(self):
        """log_stdout sends stdout lines to the logger."""
        cmd = RunCmd(["echo", "test-line"])
        with patch("fub.runcmd.logger") as mock_logger:
            cmd.log_stdout()
            # At least one call should contain our test line
            logged = [str(call) for call in mock_logger.log.call_args_list]
            assert any("test-line" in l for l in logged)

    def test_log_stderr_on_failure(self):
        """log_stderr uses ERROR level on command failure."""
        cmd = RunCmd(["sh", "-c", "echo errmsg >&2; exit 1"])
        import logging

        with patch("fub.runcmd.logger") as mock_logger:
            cmd.log_stderr()
            # Should be called with ERROR level
            logged_levels = [call[0][0] for call in mock_logger.log.call_args_list]
            assert all(level == logging.ERROR for level in logged_levels)

    def test_log_stderr_on_success(self):
        """log_stderr uses DEBUG level on command success."""
        cmd = RunCmd(["sh", "-c", "echo debugmsg >&2"])
        import logging

        with patch("fub.runcmd.logger") as mock_logger:
            cmd.log_stderr()
            logged_levels = [call[0][0] for call in mock_logger.log.call_args_list]
            assert all(level == logging.DEBUG for level in logged_levels)
