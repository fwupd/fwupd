# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Shared utilities for fub

import logging
import os
import sys
from dataclasses import dataclass, field, fields
from typing import Any, Self, get_type_hints


@dataclass(init=False)
class Yes:
    """
    Wraps the tri-state from --yes and --no commandline arguments. The third
    state is neither, i.e. "ask".
    """

    state: bool | None

    def __init__(self, args: Any) -> None:
        """Take anything that has a 'yes' attribute and parse it"""
        if args is None:
            self.state = None
        else:
            self.state = getattr(args, "yes", None)

    @property
    def yes(self) -> bool:
        return self.state is True

    @property
    def no(self) -> bool:
        return self.state is False

    @property
    def ask(self) -> bool:
        return self.state is None


@dataclass
class ConsoleColors:
    enabled: bool = True
    """
    False if colors are disabled (e.g. isatty() is false)
    """

    reset: str = "\033[0m"
    bold: str = "\033[1m"

    ansi_blue: str = "\033[94m"  # blue
    ansi_green: str = "\033[92m"  # green
    ansi_yellow: str = "\033[93m"  # yellow
    ansi_red: str = "\033[91m"  # red

    white: str = "\033[38;2;255;255;255m"  # #ffffff
    black: str = "\033[38;2;0;0;0m"  # #000000
    green: str = "\033[38;2;0;255;175m"  # #00ffaf
    blue: str = "\033[38;2;0;215;255m"  # #00d7ff
    yellow: str = "\033[38;2;255;215;0m"  # #ffd700
    pink: str = "\033[38;2;239;177;246m"  # #efb1f6

    bg_white: str = "\033[48;2;255;255;255m"  # #ffffff
    bg_black: str = "\033[48;2;0;0;0m"  # #000000
    bg_green: str = "\033[48;2;0;255;175m"  # #00ffaf
    bg_blue: str = "\033[48;2;0;215;255m"  # #00d7ff
    bg_yellow: str = "\033[48;2;255;215;0m"  # #ffd700
    bg_pink: str = "\033[48;2;239;177;246m"  # #efb1f6

    def rgb(self, red: int, green: int, blue: int) -> str:
        """
        Return the terminal escape sequence to set the background to the RGB value given
        """
        if not self.enabled:
            return ""
        return f"\033[38;2;{red};{green};{blue}m"

    def bg_rgb(self, red: int, green: int, blue: int) -> str:
        """
        Return the terminal escape sequence to set the background to the RGB value given
        """
        if not self.enabled:
            return ""
        return f"\033[48;2;{red};{green};{blue}m"

    def maybe_disable(self, use_colors: bool) -> Self:
        """
        Call this once with use_colors to False to disable
        all color sequences. Once disabled, all colors produced
        return the empty string.
        """
        if not use_colors:
            self.enabled = False
            hints = get_type_hints(type(self))
            for field in fields(self):
                if hints.get(field.name) is str:
                    setattr(self, field.name, "")
        return self


class ColorFormatter(logging.Formatter):
    """
    Formatter to be used by our logger
    """

    def format(self, record):
        level_colors = {
            logging.DEBUG: stderr_colors.ansi_blue,
            logging.INFO: stderr_colors.ansi_green,
            logging.WARNING: stderr_colors.ansi_yellow,
            logging.ERROR: stderr_colors.ansi_red,
            logging.CRITICAL: stderr_colors.ansi_red,
        }
        message = super().format(record)
        color = level_colors.get(record.levelno, "")
        if color:
            return f"{color}{record.levelname}:{stderr_colors.reset} {message}"
        return f"{record.levelname}: {message}"


@dataclass
class Printer:
    """
    A wrapper class for messages to be printed to the user
    at various levels of importance. It supports a few
    basic markup tags:
        - [bold][/bold] for highlighted text
        - [_error][/_error] for error highlighting.
          Usually it's enough to just call Printer.error()
        - [_warning][/_warning] for warning highlighting.
          Usually it's enough to just call Printer.warning()

    Nesting of markup tags is not supported.
    """

    quiet: bool = False
    """
    If set, omit messages other than warnings and errors
    """
    default_yn_answer: Yes = field(default_factory=lambda: Yes(None))
    """
    If set, answer yes or no to all y/n questions
    """

    def _parse(self, text: str, colors: ConsoleColors):
        message = text.replace("[bold]", colors.bold).replace("[/bold]", colors.reset)
        message = message.replace("[_error]", colors.ansi_red).replace(
            "[/_error]", colors.reset
        )
        message = message.replace("[_warning]", colors.ansi_yellow).replace(
            "[/_warning]", colors.reset
        )
        return message

    def message(self, text: str):
        """Print an informational message to the user"""
        if self.quiet:
            return
        msg = self._parse(text, stdout_colors)
        print(msg)

    def banner(self, text: str):
        """Print a divider with optional text to break up the
        output for humans"""
        if self.quiet:
            return
        if stdout_colors.enabled:
            fg = stdout_colors.black
            reset = stdout_colors.reset
            color = stdout_colors.blue
            bgcolor = stdout_colors.bg_blue
            pad = "               "
            # Nerd font arrows: \ue0b2 () and \ue0b0 ()
            larrow = "\ue0b2"
            rarrow = "\ue0b0"
            msg = self._parse(text, stdout_colors)
            print(
                f"{color}{larrow}{bgcolor}{fg}"
                f"{pad}{msg}{pad}"
                f"{reset}{color}{rarrow}{reset}\n"
            )
        else:
            msg = self._parse(text, stdout_colors)
            print(f".......::::::: {msg} :::::::.......")

    def warning(self, text: str):
        """Print a warning to the user. This warning is highlighted by default."""
        msg = self._parse(f"[_warning]Warning:[/_warning] {text}", stderr_colors)
        print(msg, file=sys.stderr)

    def error(self, text: str):
        """Print a warning to the user. This error is highlighted by default."""
        msg = self._parse(f"[_error]Error:[/_error] {text}", stderr_colors)
        print(msg, file=sys.stderr)

    def ask(self, question: str) -> str | None:
        """Ask a question, collect the answer"""
        try:
            answer = input(f"{question} ")
            return answer
        except EOFError:
            return None

    def ask_yn(self, question: str) -> bool:
        """Ask a y/n question, collect the answer"""
        if self.default_yn_answer.yes:
            return True
        if self.default_yn_answer.no:
            return False

        answer = self.ask(question)
        return answer is not None and answer.lower() == "y"


# Respect NO_COLOR (https://no-color.org) and non-tty output
stdout_use_colors = os.environ.get("FORCE_COLOR") is not None or (
    sys.stdout.isatty() and not os.environ.get("NO_COLOR")
)
stderr_use_colors = os.environ.get("FORCE_COLOR") is not None or (
    sys.stderr.isatty() and not os.environ.get("NO_COLOR")
)

stdout_colors = ConsoleColors().maybe_disable(stdout_use_colors)
stderr_colors = ConsoleColors().maybe_disable(stderr_use_colors)
logger = logging.getLogger("fub")
printer = Printer()
