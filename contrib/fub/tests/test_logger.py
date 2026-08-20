# SPDX-License-Identifier: LGPL-2.1-or-later

import logging
from dataclasses import fields
from typing import get_type_hints
from unittest.mock import MagicMock, patch

import pytest
from fub.logger import ColorFormatter, ConsoleColors, Printer, Yes


class TestYes:
    def test_yes_state(self):
        """When args.yes is True, .yes is True and .no/.ask are False."""
        args = MagicMock()
        args.yes = True
        y = Yes(args)
        assert y.yes is True
        assert y.no is False
        assert y.ask is False

    def test_no_state(self):
        """When args.yes is False, .no is True and .yes/.ask are False."""
        args = MagicMock()
        args.yes = False
        y = Yes(args)
        assert y.yes is False
        assert y.no is True
        assert y.ask is False

    def test_ask_state(self):
        """When args.yes is None, .ask is True and .yes/.no are False."""
        args = MagicMock()
        args.yes = None
        y = Yes(args)
        assert y.yes is False
        assert y.no is False
        assert y.ask is True

    def test_none_args(self):
        """Passing None as args sets state to None (ask mode)."""
        y = Yes(None)
        assert y.ask is True
        assert y.yes is False
        assert y.no is False

    def test_args_without_yes_attr(self):
        """When args has no 'yes' attribute, state is None (ask mode)."""
        args = MagicMock(spec=[])  # no attributes
        y = Yes(args)
        assert y.ask is True


class TestConsoleColors:
    def test_default_enabled(self):
        """ConsoleColors is enabled by default."""
        c = ConsoleColors()
        assert c.enabled is True

    def test_maybe_disable_clears_strings(self):
        """maybe_disable(False) sets all str fields to empty string."""
        c = ConsoleColors().maybe_disable(use_colors=False)
        hints = get_type_hints(type(c))
        for f in fields(c):
            if hints.get(f.name) is str:  # skip over enabled which is a bool
                assert getattr(c, f.name) == "", f"field {f.name} should be empty"

    def test_maybe_disable_keeps_when_enabled(self):
        """maybe_disable(True) leaves all values intact."""
        c = ConsoleColors().maybe_disable(use_colors=True)
        assert c.reset == "\033[0m"
        assert c.bold == "\033[1m"
        assert c.enabled is True

    def test_rgb_returns_escape_code(self):
        """rgb() returns an ANSI escape sequence when enabled."""
        c = ConsoleColors()
        result = c.rgb(255, 128, 0)
        assert result == "\033[38;2;255;128;0m"

    def test_rgb_disabled_returns_empty(self):
        """rgb() returns empty string when enabled is False."""
        c = ConsoleColors(enabled=False)
        result = c.rgb(255, 128, 0)
        assert result == ""

    def test_bg_rgb_returns_escape_code(self):
        """bg_rgb() returns a background ANSI escape sequence."""
        c = ConsoleColors()
        result = c.bg_rgb(0, 128, 255)
        assert result == "\033[48;2;0;128;255m"

    def test_bg_rgb_disabled_returns_empty(self):
        """bg_rgb() returns empty string when enabled is False."""
        c = ConsoleColors(enabled=False)
        result = c.bg_rgb(0, 128, 255)
        assert result == ""


class TestColorFormatter:
    @pytest.mark.parametrize(
        "level, expected_color_prefix",
        [
            (logging.DEBUG, "\033[94m"),
            (logging.INFO, "\033[92m"),
            (logging.WARNING, "\033[93m"),
            (logging.ERROR, "\033[91m"),
            (logging.CRITICAL, "\033[91m"),
        ],
        ids=[
            "debug-blue",
            "info-green",
            "warning-yellow",
            "error-red",
            "critical-red",
        ],
    )
    def test_format_includes_level_color(self, level, expected_color_prefix):
        """Each log level gets the correct color prefix."""
        formatter = ColorFormatter()
        record = logging.LogRecord(
            name="test",
            level=level,
            pathname="",
            lineno=0,
            msg="test message",
            args=(),
            exc_info=None,
        )
        # Ensure stderr_colors is enabled for this test
        with patch("fub.logger.stderr_colors", ConsoleColors()):
            result = formatter.format(record)
            assert expected_color_prefix in result
            assert "test message" in result


class TestPrinter:
    def test_parse_bold_markup(self):
        """_parse replaces [bold]/[/bold] with escape codes."""
        p = Printer()
        colors = ConsoleColors()
        result = p._parse("[bold]text[/bold]", colors)
        assert colors.bold in result
        assert colors.reset in result
        assert "text" in result

    def test_parse_error_markup(self):
        """_parse replaces [_error]/[/_error] with red escape codes."""
        p = Printer()
        colors = ConsoleColors()
        result = p._parse("[_error]text[/_error]", colors)
        assert colors.ansi_red in result
        assert colors.reset in result

    def test_parse_no_markup(self):
        """_parse returns plain text unchanged when no markup present."""
        p = Printer()
        colors = ConsoleColors()
        result = p._parse("plain text", colors)
        assert result == "plain text"

    def test_message_prints_to_stdout(self, capsys):
        """message() prints to stdout."""
        p = Printer()
        with patch("fub.logger.stdout_colors", ConsoleColors().maybe_disable(False)):
            p.message("hello world")
        captured = capsys.readouterr()
        assert "hello world" in captured.out

    def test_ask_yn_yes_default(self):
        """ask_yn returns True without prompting when default is yes."""
        args = MagicMock()
        args.yes = True
        p = Printer(default_yn_answer=Yes(args))
        assert p.ask_yn("Continue?") is True

    def test_ask_yn_no_default(self):
        """ask_yn returns False without prompting when default is no."""
        args = MagicMock()
        args.yes = False
        p = Printer(default_yn_answer=Yes(args))
        assert p.ask_yn("Continue?") is False

    def test_ask_yn_interactive_yes(self):
        """ask_yn returns True when user types 'y'."""
        p = Printer()
        with patch("builtins.input", return_value="y"):
            assert p.ask_yn("Continue?") is True

    def test_ask_yn_interactive_no(self):
        """ask_yn returns False when user types 'n'."""
        p = Printer()
        with patch("builtins.input", return_value="n"):
            assert p.ask_yn("Continue?") is False

    def test_ask_returns_input(self):
        """ask() returns the user's input."""
        p = Printer()
        with patch("builtins.input", return_value="my answer"):
            result = p.ask("Question?")
            assert result == "my answer"

    def test_ask_returns_none_on_eof(self):
        """ask() returns None on EOFError."""
        p = Printer()
        with patch("builtins.input", side_effect=EOFError):
            result = p.ask("Question?")
            assert result is None

    def test_banner_with_colors(self, capsys):
        """banner() prints a decorated banner when colors are enabled."""
        p = Printer()
        colors = ConsoleColors()
        with patch("fub.logger.stdout_colors", colors):
            p.banner("Build")
        captured = capsys.readouterr()
        assert "Build" in captured.out

    def test_banner_without_colors(self, capsys):
        """banner() prints a plain text banner when colors are disabled."""
        p = Printer()
        colors = ConsoleColors(enabled=False)
        with patch("fub.logger.stdout_colors", colors):
            p.banner("Build")
        captured = capsys.readouterr()
        assert "Build" in captured.out
        assert ":::::::" in captured.out
