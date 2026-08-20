# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Subprocess execution wrapper for fub

import logging
import shutil
import subprocess
from typing import Self

from .logger import logger


class SudoMissing(Exception):
    """Raised when sudo is required but not found in PATH."""


class RunCmd:
    """
    Wrapper around a system command.

    Runs on creation and keeps the exit status and any stdout/stderr for
    future usage (unless capture is False).

    kwargs can be anything to be passed to subprocess.run().
    """

    returncode: int = 1

    def __init__(
        self,
        args,
        *,
        check: bool = False,
        capture: bool = True,
        text: bool = True,
        as_sudo: bool = False,
        sudo_env: dict[str, str] | None = None,
        daemon: bool = False,
        **kwargs,
    ):
        args = [str(a) for a in args]

        if as_sudo:
            sudo = shutil.which("sudo")
            if not sudo:
                raise SudoMissing()
            sudo_cmd = [sudo]
            for k, v in (sudo_env or {}).items():
                sudo_cmd.append(f"{k}={v}")
            args = sudo_cmd + args

        logger.debug(f"Running: {' '.join(args)}")
        if capture:
            kwargs.setdefault("stdout", subprocess.PIPE)
            kwargs.setdefault("stderr", subprocess.PIPE)
        if "cwd" in kwargs and not isinstance(kwargs["cwd"], str):
            kwargs["cwd"] = str(kwargs["cwd"])

        self.daemon_process = None
        if daemon:
            self.daemon_process = subprocess.Popen(args, text=text, **kwargs)
        else:
            try:
                self.p = subprocess.run(args, check=check, text=text, **kwargs)
                self.returncode = self.p.returncode
                if capture:
                    self.log_stdout()
                    self.log_stderr()
            except FileNotFoundError as error:
                if check:
                    raise
                self.p = subprocess.CompletedProcess(
                    args, 1, stdout="", stderr=str(error)
                )
                self.returncode = 1

    def kill(self):
        if self.daemon_process:
            import signal
            import time

            self.daemon_process.send_signal(signal.SIGTERM)
            for _ in range(5):
                if self.daemon_process.poll() is not None:
                    break
                time.sleep(0.3)
            try:
                self.daemon_process.kill()
            except ProcessLookupError:
                pass  # Process already terminated
            self.daemon_process.wait()

    def __enter__(self) -> Self:
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.kill()

    def log_stdout(self, level: int = logging.DEBUG):
        """
        Log stdout to the global logger, at debug level by default
        """
        if self.p.stdout:
            for line in self.p.stdout.split("\n"):
                logger.log(level, line)

    def log_stderr(self, level: int | None = None):
        """
        Log stdout to the global logger, at debug level on success
        and error on failure.

        If a custom log level is given, log at that level instead.
        """
        if not self.p.stderr:
            return

        if level is None:
            level = logging.DEBUG if self.success else logging.ERROR

        for line in self.p.stderr.split("\n"):
            logger.log(level, line)

    @property
    def stdout(self) -> str:
        """
        The completed process' stdout as single string
        """
        return self.p.stdout

    @property
    def stderr(self) -> str:
        """
        The completed process' stderr as single string
        """
        return self.p.stderr

    @property
    def success(self) -> bool:
        """
        True if exited with success, False otherwise
        """
        return self.p.returncode == 0
