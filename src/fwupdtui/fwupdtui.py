#!/usr/bin/env python3
#
# Copyright 2026 Advanced Micro Devices, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""A Textual interface for viewing and changing firmware settings."""

import argparse
import datetime
import gettext
import html
import os
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

import gi
from rich.markup import escape
from rich.text import Text
from textual import events
from textual.app import App, ComposeResult
from textual.containers import Grid, Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import (
    Button,
    ContentSwitcher,
    Footer,
    Header,
    Input,
    Label,
    Select,
    Static,
    Tree,
)

# A custom prefix such as /usr/local can contain a newer libfwupd than the
# distro typelib in the default GI search path.  Keep the library and typelib
# from the same installation unless the caller selected a path explicitly.
TYPELIBDIR = "@libdir@/girepository-1.0"
if "GI_TYPELIB_PATH" not in os.environ and pathlib.Path(TYPELIBDIR).is_dir():
    os.environ["GI_TYPELIB_PATH"] = TYPELIBDIR

gi.require_version("Fwupd", "2.0")

from gi.repository import Fwupd, GLib  # noqa: E402

PACKAGE_VERSION = "@FWUPD_VERSION@"
LOCALEDIR = os.environ.get("FWUPD_LOCALEDIR", "@localedir@")
gettext.bindtextdomain("fwupd", LOCALEDIR)
gettext.textdomain("fwupd")
_ = gettext.gettext


@dataclass(frozen=True)
class BiosSetting:
    """The subset of FwupdBiosSetting used by the interface."""

    key: str
    name: str
    parent: str
    description: str
    current_value: str
    kind: Fwupd.BiosSettingKind
    read_only: bool
    possible_values: List[str]
    lower_bound: int
    upper_bound: int
    scalar_increment: int

    @classmethod
    def from_fwupd(cls, setting) -> "BiosSetting":
        """Create an immutable view from an introspected FwupdBiosSetting."""
        name = setting.get_name() or _("Unnamed setting")
        description = setting.get_description()
        kind = setting.get_kind()
        possible_values = []
        if kind == Fwupd.BiosSettingKind.ENUMERATION:
            possible_values = list(setting.get_possible_values() or [])
        return cls(
            key=setting.get_id() or name,
            name=name,
            # get_parent() was added to the introspection API alongside this
            # application.  Keep the TUI usable with an older system typelib
            # when running it directly from a build tree.
            parent=(
                (setting.get_parent() or "") if hasattr(setting, "get_parent") else ""
            ),
            description=_(description) if description else "",
            current_value=setting.get_current_value() or "",
            kind=kind,
            read_only=setting.get_read_only(),
            possible_values=possible_values,
            lower_bound=setting.get_lower_bound(),
            upper_bound=setting.get_upper_bound(),
            scalar_increment=max(setting.get_scalar_increment(), 1),
        )

    def validate(self, value: str) -> str:
        """Validate and normalize a value before sending it to fwupd."""
        if self.kind == Fwupd.BiosSettingKind.ENUMERATION:
            if value not in self.possible_values:
                raise ValueError(_("Choose one of the available values"))
            return value
        if self.kind == Fwupd.BiosSettingKind.INTEGER:
            try:
                number = int(value, 0)
            except ValueError as error:
                raise ValueError(_("Enter a valid integer")) from error
            if not self.lower_bound <= number <= self.upper_bound:
                raise ValueError(
                    _("Value must be between {lower} and {upper}").format(
                        lower=self.lower_bound, upper=self.upper_bound
                    )
                )
            if (number - self.lower_bound) % self.scalar_increment != 0:
                raise ValueError(
                    _("Value must use an increment of {increment}").format(
                        increment=self.scalar_increment
                    )
                )
            return str(number)
        if self.kind == Fwupd.BiosSettingKind.STRING:
            if not self.lower_bound <= len(value) <= self.upper_bound:
                raise ValueError(
                    _("Text length must be between {lower} and {upper}").format(
                        lower=self.lower_bound, upper=self.upper_bound
                    )
                )
            return value
        raise ValueError(_("This setting type cannot be changed"))


class SettingEditor(ModalScreen):
    """Modal editor for a single BIOS setting."""

    CSS = """
    SettingEditor {
        align: center middle;
    }
    SettingEditor > Vertical {
        width: 70;
        height: auto;
        max-height: 24;
        border: round $accent;
        background: $surface;
        padding: 1 2;
    }
    SettingEditor .editor-title {
        text-style: bold;
        margin-bottom: 1;
    }
    SettingEditor #editor-error {
        color: $error;
        min-height: 1;
        margin-top: 1;
    }
    SettingEditor Horizontal {
        height: auto;
        align-horizontal: right;
        margin-top: 1;
    }
    SettingEditor Button {
        margin-left: 1;
    }
    """

    def __init__(self, setting: BiosSetting, value: str) -> None:
        super().__init__()
        self.setting = setting
        self.value = value

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(self.setting.name, classes="editor-title")
            if self.setting.kind == Fwupd.BiosSettingKind.ENUMERATION:
                options = [(value, value) for value in self.setting.possible_values]
                if self.value and self.value in self.setting.possible_values:
                    yield Select(
                        options,
                        value=self.value,
                        allow_blank=True,
                        id="editor-value",
                    )
                else:
                    yield Select(options, allow_blank=True, id="editor-value")
            else:
                yield Input(value=self.value, id="editor-input")
            yield Static("", id="editor-error")
            with Horizontal():
                yield Button(_("Cancel"), id="editor-cancel")
                yield Button(_("Save"), id="editor-save", variant="primary")

    def on_mount(self) -> None:
        if self.setting.kind == Fwupd.BiosSettingKind.ENUMERATION:
            self.query_one("#editor-value", Select).focus()
        else:
            self.query_one("#editor-input", Input).focus()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "editor-cancel":
            self.dismiss(None)
            return
        if self.setting.kind == Fwupd.BiosSettingKind.ENUMERATION:
            value = self.query_one("#editor-value", Select).value
            value = str(value) if value in self.setting.possible_values else ""
        else:
            value = self.query_one("#editor-input", Input).value
        try:
            self.dismiss(self.setting.validate(value))
        except ValueError as error:
            self.query_one("#editor-error", Static).update(str(error))


class ConfirmAction(ModalScreen):
    """Ask the user to confirm an operation that changes firmware state."""

    CSS = """
    ConfirmAction {
        align: center middle;
    }
    ConfirmAction > Vertical {
        width: 76;
        height: auto;
        max-height: 28;
        border: round $warning;
        background: $surface;
        padding: 1 2;
    }
    ConfirmAction .dialog-title {
        text-style: bold;
        margin-bottom: 1;
    }
    ConfirmAction Horizontal {
        height: auto;
        align-horizontal: right;
        margin-top: 1;
    }
    ConfirmAction Button {
        margin-left: 1;
    }
    """

    def __init__(self, title: str, message: str, action: str) -> None:
        super().__init__()
        self.dialog_title = title
        self.message = message
        self.action = action

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(self.dialog_title, classes="dialog-title")
            yield Static(self.message)
            with Horizontal():
                yield Button(_("Cancel"), id="confirm-cancel")
                yield Button(self.action, id="confirm-action", variant="warning")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(event.button.id == "confirm-action")


class PathPrompt(ModalScreen):
    """Prompt for a local firmware archive path."""

    CSS = """
    PathPrompt {
        align: center middle;
    }
    PathPrompt > Vertical {
        width: 76;
        height: auto;
        border: round $accent;
        background: $surface;
        padding: 1 2;
    }
    PathPrompt .dialog-title {
        text-style: bold;
        margin-bottom: 1;
    }
    PathPrompt #path-error {
        color: $error;
        min-height: 1;
    }
    PathPrompt Horizontal {
        height: auto;
        align-horizontal: right;
        margin-top: 1;
    }
    PathPrompt Button {
        margin-left: 1;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(_("Install Local Firmware"), classes="dialog-title")
            yield Input(placeholder=_("Path to a .cab firmware archive"), id="cab-path")
            yield Static("", id="path-error")
            with Horizontal():
                yield Button(_("Cancel"), id="path-cancel")
                yield Button(_("Continue"), id="path-continue", variant="primary")

    def on_mount(self) -> None:
        self.query_one("#cab-path", Input).focus()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "path-cancel":
            self.dismiss(None)
            return
        path = pathlib.Path(self.query_one("#cab-path", Input).value).expanduser()
        if not path.is_file():
            self.query_one("#path-error", Static).update(_("File not found"))
            return
        self.dismiss(str(path))


def xml_to_text(value: Optional[str]) -> str:
    """Convert the small AppStream markup subset used by release descriptions."""
    if not value:
        return ""
    value = re.sub(r"</?(?:p|ul|ol|li|em|strong|code)[^>]*>", " ", value)
    value = re.sub(r"<[^>]+>", "", value)
    return " ".join(html.unescape(value).split())


def format_size(size: int) -> str:
    """Format a byte count for display."""
    value = float(size)
    for unit in (_("bytes"), _("KB"), _("MB"), _("GB")):
        if value < 1024 or unit == _("GB"):
            return (
                f"{value:.1f} {unit}" if unit != _("bytes") else f"{int(value)} {unit}"
            )
        value /= 1024
    return str(size)


def format_duration(seconds: int) -> str:
    """Format an estimated operation duration."""
    if seconds <= 0:
        return ""
    if seconds < 60:
        return _("{seconds} seconds").format(seconds=seconds)
    return _("{minutes} minutes").format(minutes=max(1, round(seconds / 60)))


SECURITY_RESULTS = {
    Fwupd.SecurityAttrResult.VALID: _("Valid"),
    Fwupd.SecurityAttrResult.NOT_VALID: _("Invalid"),
    Fwupd.SecurityAttrResult.ENABLED: _("Enabled"),
    Fwupd.SecurityAttrResult.NOT_ENABLED: _("Disabled"),
    Fwupd.SecurityAttrResult.LOCKED: _("Locked"),
    Fwupd.SecurityAttrResult.NOT_LOCKED: _("Unlocked"),
    Fwupd.SecurityAttrResult.ENCRYPTED: _("Encrypted"),
    Fwupd.SecurityAttrResult.NOT_ENCRYPTED: _("Unencrypted"),
    Fwupd.SecurityAttrResult.TAINTED: _("Tainted"),
    Fwupd.SecurityAttrResult.NOT_TAINTED: _("Untainted"),
    Fwupd.SecurityAttrResult.FOUND: _("Found"),
    Fwupd.SecurityAttrResult.NOT_FOUND: _("Not found"),
    Fwupd.SecurityAttrResult.SUPPORTED: _("Supported"),
    Fwupd.SecurityAttrResult.NOT_SUPPORTED: _("Not supported"),
}

SECURITY_LEVELS = {
    Fwupd.SecurityAttrLevel.CRITICAL: _("HSI-1 Critical"),
    Fwupd.SecurityAttrLevel.IMPORTANT: _("HSI-2 Important"),
    Fwupd.SecurityAttrLevel.THEORETICAL: _("HSI-3 Theoretical"),
    Fwupd.SecurityAttrLevel.SYSTEM_PROTECTION: _("HSI-4 System Protection"),
    Fwupd.SecurityAttrLevel.SYSTEM_ATTESTATION: _("HSI-5 System Attestation"),
}


class FwupdTui(App):
    """Terminal interface for firmware, BIOS, and host-security management."""

    TITLE = _("fwupd")
    SUB_TITLE = _("Firmware management")

    HOME_BUTTON_IDS = (
        "home-bios",
        "home-devices",
        "home-security",
        "home-quit",
    )

    CSS = """
    #views, .view {
        height: 1fr;
    }
    #home-view {
        align: center middle;
        padding: 2;
    }
    #home-menu {
        width: 62;
        height: auto;
        border: round $primary;
        padding: 1 3 2 3;
    }
    #home-title {
        text-align: center;
        text-style: bold;
        margin-bottom: 1;
    }
    #home-menu Button {
        width: 100%;
        margin-top: 1;
    }
    FwupdTui Button:disabled {
        display: none;
    }
    .browser {
        width: 2fr;
        min-width: 32;
        border-right: solid $primary;
    }
    .search {
        margin: 1;
    }
    .browser-tree {
        height: 1fr;
        padding: 0 1;
    }
    .details-pane {
        width: 3fr;
        min-width: 38;
        padding: 1 2;
    }
    .details {
        height: 1fr;
        overflow-y: auto;
    }
    .status {
        height: auto;
        min-height: 1;
        color: $text-muted;
        margin-bottom: 1;
    }
    .buttons {
        height: auto;
        align-horizontal: right;
    }
    .buttons Button {
        margin-left: 1;
        min-width: 12;
    }
    #release-tree {
        height: 10;
        border-top: solid $primary;
        margin-top: 1;
    }
    #device-actions-secondary {
        height: auto;
        align-horizontal: right;
    }
    #device-actions-secondary Button {
        margin-left: 1;
    }
    #device-actions {
        height: 3;
        layout: grid;
        grid-size: 6;
        grid-columns: 1fr;
        grid-rows: 3;
        grid-gutter: 0 1;
    }
    #device-actions Button {
        width: 100%;
        min-width: 0;
    }
    FwupdTui.compact #device-actions {
        height: 6;
        grid-size: 3;
    }
    FwupdTui.narrow #device-actions {
        height: 9;
        grid-size: 2;
    }
    FwupdTui.narrow .browser {
        min-width: 24;
    }
    FwupdTui.narrow .details-pane {
        min-width: 30;
    }
    FwupdTui.short #release-tree,
    FwupdTui.short #security-events {
        height: 6;
    }
    FwupdTui.short .details-pane {
        padding: 0 1;
    }
    #security-summary {
        height: auto;
        margin-bottom: 1;
    }
    #security-events {
        height: 12;
        border-top: solid $primary;
        padding-top: 1;
        overflow-y: auto;
    }
    """

    BINDINGS = [
        ("q", "quit", _("Quit")),
        ("escape", "home", _("Home")),
        ("slash", "focus_search", _("Search")),
        ("e", "edit", _("Edit")),
        ("ctrl+s", "apply", _("Apply")),
        ("r", "refresh", _("Refresh")),
    ]

    def __init__(
        self,
        settings: Optional[List[BiosSetting]] = None,
        client=None,
    ) -> None:
        super().__init__()
        self.client = client
        self.settings_loaded = settings is not None
        self.settings = sorted(
            settings or [], key=lambda item: (item.parent, item.name)
        )
        self.settings_by_key = {setting.key: setting for setting in self.settings}
        self.pending: Dict[str, str] = {}
        self.current_key: Optional[str] = None
        self.devices: Dict[str, object] = {}
        self.current_device_id: Optional[str] = None
        self.releases: Dict[str, object] = {}
        self.current_release_key: Optional[str] = None
        self.security_attrs: Dict[str, object] = {}

    def compose(self) -> ComposeResult:
        yield Header()
        with ContentSwitcher(initial="home-view", id="views"):
            with Vertical(id="home-view", classes="view"):
                with Vertical(id="home-menu"):
                    yield Label(_("Firmware Management"), id="home-title")
                    yield Static(
                        _("Choose what you would like to inspect or change."),
                    )
                    yield Static(
                        _("Use ↑/↓ and Enter, or press 1, 2, or 3."),
                    )
                    yield Button(_("Manage BIOS Settings"), id="home-bios")
                    yield Button(_("Manage Devices"), id="home-devices")
                    yield Button(_("View Security Measurement"), id="home-security")
                    yield Button(_("Quit"), id="home-quit")
            with Horizontal(id="bios-view", classes="view"):
                with Vertical(classes="browser"):
                    yield Input(
                        placeholder=_("Search BIOS settings"),
                        id="search",
                        classes="search",
                    )
                    yield Tree(
                        _("BIOS Settings"),
                        id="settings-tree",
                        classes="browser-tree",
                    )
                with Vertical(classes="details-pane"):
                    yield Static(
                        _("Select a setting to view its details."),
                        id="details",
                        classes="details",
                    )
                    yield Static("", id="status", classes="status")
                    with Horizontal(classes="buttons"):
                        yield Button(_("Home"), id="bios-home")
                        yield Button(_("Edit"), id="edit", disabled=True)
                        yield Button(
                            _("Apply"), id="apply", variant="success", disabled=True
                        )
            with Horizontal(id="devices-view", classes="view"):
                with Vertical(classes="browser"):
                    yield Input(
                        placeholder=_("Search devices"),
                        id="device-search",
                        classes="search",
                    )
                    yield Tree(_("Devices"), id="device-tree", classes="browser-tree")
                    with Horizontal(id="device-actions-secondary"):
                        yield Button(_("Refresh"), id="devices-refresh")
                        yield Button(_("Refresh Metadata"), id="metadata-refresh")
                    with Horizontal():
                        yield Button(_("Enable LVFS"), id="enable-lvfs", disabled=True)
                        yield Button(_("Install Local File"), id="install-file")
                with Vertical(classes="details-pane"):
                    yield Static(
                        _("Select a device to view its details."),
                        id="device-details",
                        classes="details",
                    )
                    yield Tree(_("Available Releases"), id="release-tree")
                    yield Static("", id="device-status", classes="status")
                    with Grid(id="device-actions"):
                        yield Button(_("Home"), id="devices-home")
                        yield Button(_("Unlock"), id="device-unlock", disabled=True)
                        yield Button(_("Verify"), id="device-verify", disabled=True)
                        yield Button(
                            _("Store"),
                            id="device-verify-update",
                            disabled=True,
                        )
                        yield Button(_("Activate"), id="device-activate", disabled=True)
                        yield Button(
                            _("Install"),
                            id="release-install",
                            variant="success",
                            disabled=True,
                        )
            with Horizontal(id="security-view", classes="view"):
                with Vertical(classes="browser"):
                    yield Tree(
                        _("Security Attributes"),
                        id="security-tree",
                        classes="browser-tree",
                    )
                with Vertical(classes="details-pane"):
                    yield Static("", id="security-summary")
                    yield Static(
                        _("Select a security attribute to view its details."),
                        id="security-details",
                        classes="details",
                    )
                    yield Static("", id="security-events")
                    yield Static("", id="security-status", classes="status")
                    with Horizontal(classes="buttons"):
                        yield Button(_("Home"), id="security-home")
                        yield Button(_("Refresh"), id="security-refresh")
        yield Footer()

    def on_mount(self) -> None:
        self.rebuild_tree()
        for button in self.query(Button):
            if button.disabled:
                button.display = False
        home_bios = self.query_one("#home-bios", Button)
        home_bios.variant = "primary"
        home_bios.focus()

    def on_resize(self, event: events.Resize) -> None:
        self.set_class(event.size.width < 140, "compact")
        self.set_class(event.size.width < 90, "narrow")
        self.set_class(event.size.height < 32, "short")

    def on_descendant_focus(self, event: events.DescendantFocus) -> None:
        if event.widget.id not in self.HOME_BUTTON_IDS:
            return
        for button in self.query("#home-menu Button"):
            button.variant = "primary" if button is event.widget else "default"

    @staticmethod
    def set_button_available(button: Button, available: bool) -> None:
        button.disabled = not available
        button.display = available

    @property
    def current_view(self) -> str:
        return self.query_one("#views", ContentSwitcher).current or "home-view"

    def show_view(self, view: str) -> None:
        self.query_one("#views", ContentSwitcher).current = view
        if view == "bios-view":
            self.load_bios_settings()
            self.query_one("#settings-tree", Tree).focus()
        elif view == "devices-view":
            self.load_devices()
            self.query_one("#device-tree", Tree).focus()
        elif view == "security-view":
            self.load_security()
            self.query_one("#security-tree", Tree).focus()
        else:
            self.query_one("#home-bios", Button).focus()

    def action_home(self) -> None:
        if self.current_view == "home-view":
            return
        self.show_view("home-view")

    def action_focus_search(self) -> None:
        if self.current_view == "bios-view":
            self.query_one("#search", Input).focus()
        elif self.current_view == "devices-view":
            self.query_one("#device-search", Input).focus()

    def action_refresh(self) -> None:
        if self.current_view == "bios-view":
            self.settings_loaded = False
            self.load_bios_settings()
        elif self.current_view == "devices-view":
            self.load_devices()
        elif self.current_view == "security-view":
            self.load_security()

    def on_key(self, event: events.Key) -> None:
        # Modal widgets, especially Select's overlay, must handle Escape and
        # navigation keys without querying widgets on the underlying screen.
        if isinstance(self.screen, ModalScreen):
            return
        if self.current_view != "home-view":
            return
        buttons = [
            self.query_one(f"#{button_id}", Button)
            for button_id in self.HOME_BUTTON_IDS
        ]
        focused = self.focused if self.focused in buttons else buttons[0]
        if event.key in ("down", "j"):
            buttons[(buttons.index(focused) + 1) % len(buttons)].focus()
        elif event.key in ("up", "k"):
            buttons[(buttons.index(focused) - 1) % len(buttons)].focus()
        elif event.key in ("1", "b"):
            buttons[0].press()
        elif event.key in ("2", "d"):
            buttons[1].press()
        elif event.key in ("3", "s"):
            buttons[2].press()
        else:
            return
        event.prevent_default()
        event.stop()

    # BIOS settings ---------------------------------------------------------

    def load_bios_settings(self) -> None:
        if self.settings_loaded:
            self.rebuild_tree(self.query_one("#search", Input).value)
            return
        if self.client is None:
            self.query_one("#status", Static).update(_("fwupd is not connected"))
            return
        try:
            self.settings = sorted(
                load_settings(self.client), key=lambda item: (item.parent, item.name)
            )
        except GLib.Error as error:
            self.query_one("#status", Static).update(
                _("Failed to load BIOS settings: {error}").format(error=error.message)
            )
            return
        self.settings_loaded = True
        self.settings_by_key = {setting.key: setting for setting in self.settings}
        self.rebuild_tree(self.query_one("#search", Input).value)

    def rebuild_tree(self, search: str = "") -> None:
        tree = self.query_one("#settings-tree", Tree)
        tree.clear()
        tree.show_root = False
        groups = {"": tree.root}
        search_folded = search.casefold()
        visible = 0
        for setting in self.settings:
            searchable = " ".join(
                (
                    setting.name,
                    setting.parent,
                    setting.description,
                    setting.current_value,
                )
            ).casefold()
            if search_folded and search_folded not in searchable:
                continue
            parent_path = ""
            parent_node = tree.root
            for component in filter(None, setting.parent.split("/")):
                parent_path = f"{parent_path}/{component}" if parent_path else component
                if parent_path not in groups:
                    groups[parent_path] = parent_node.add(Text(component), expand=True)
                parent_node = groups[parent_path]
            prefix = "● " if setting.key in self.pending else ""
            parent_node.add_leaf(Text(f"{prefix}{setting.name}"), data=setting.key)
            visible += 1
        tree.root.expand()
        message = _("{count} settings").format(count=visible)
        if self.settings_loaded and not self.settings:
            message = _("This system does not expose BIOS settings")
        self.query_one("#status", Static).update(message)

    def show_details(self, key: Optional[str]) -> None:
        self.current_key = key
        details = self.query_one("#details", Static)
        edit = self.query_one("#edit", Button)
        if key is None:
            details.update(_("Select a setting to view its details."))
            self.set_button_available(edit, False)
            return
        setting = self.settings_by_key[key]
        pending = self.pending.get(key)
        lines = [f"[b]{escape(setting.name)}[/b]"]
        if setting.parent:
            lines.append(escape(setting.parent.replace("/", " / ")))
        if setting.description:
            lines.extend(("", escape(setting.description)))
        lines.extend(
            (
                "",
                f"[b]{escape(_('Current value'))}:[/b] {escape(setting.current_value)}",
            )
        )
        if pending is not None:
            lines.append(
                f"[b]{escape(_('Pending value'))}:[/b] [yellow]{escape(pending)}[/yellow]"
            )
        if setting.kind == Fwupd.BiosSettingKind.ENUMERATION:
            lines.extend(("", f"[b]{escape(_('Available values'))}[/b]"))
            lines.extend(f" • {escape(value)}" for value in setting.possible_values)
        elif setting.kind == Fwupd.BiosSettingKind.INTEGER:
            lines.append(
                _("Range: {lower}–{upper}, increment {increment}").format(
                    lower=setting.lower_bound,
                    upper=setting.upper_bound,
                    increment=setting.scalar_increment,
                )
            )
        elif setting.kind == Fwupd.BiosSettingKind.STRING:
            lines.append(
                _("Length: {lower}–{upper} characters").format(
                    lower=setting.lower_bound, upper=setting.upper_bound
                )
            )
        if setting.read_only:
            lines.extend(("", f"[dim]{escape(_('This setting is read-only.'))}[/dim]"))
        details.update("\n".join(lines))
        self.set_button_available(edit, not setting.read_only)

    def edit_complete(self, value: Optional[str]) -> None:
        if value is None or self.current_key is None:
            return
        setting = self.settings_by_key[self.current_key]
        if value == setting.current_value:
            self.pending.pop(setting.key, None)
        else:
            self.pending[setting.key] = value
        self.set_button_available(self.query_one("#apply", Button), bool(self.pending))
        search = self.query_one("#search", Input).value
        self.rebuild_tree(search)
        self.show_details(setting.key)

    def action_edit(self) -> None:
        if self.current_view != "bios-view" or self.current_key is None:
            return
        setting = self.settings_by_key[self.current_key]
        if setting.read_only:
            return
        value = self.pending.get(setting.key, setting.current_value)
        self.push_screen(SettingEditor(setting, value), self.edit_complete)

    def action_apply(self) -> None:
        if self.current_view != "bios-view" or not self.pending:
            return
        if self.client is None:
            self.query_one("#status", Static).update(_("fwupd is not connected"))
            return
        try:
            self.client.modify_bios_setting(dict(self.pending))
        except GLib.Error as error:
            self.query_one("#status", Static).update(
                _("Failed to modify BIOS settings: {error}").format(error=error.message)
            )
            return
        self.pending.clear()
        self.set_button_available(self.query_one("#apply", Button), False)
        self.settings_loaded = False
        self.load_bios_settings()
        self.query_one("#status", Static).update(
            _("BIOS settings changed successfully; a reboot may be required")
        )

    # Devices ---------------------------------------------------------------

    def load_devices(self) -> None:
        tree = self.query_one("#device-tree", Tree)
        tree.clear()
        tree.show_root = False
        self.devices.clear()
        self.current_device_id = None
        self.releases.clear()
        self.query_one("#release-tree", Tree).clear()
        if self.client is None:
            self.query_one("#device-status", Static).update(_("fwupd is not connected"))
            return
        try:
            devices = list(self.client.get_devices() or [])
        except GLib.Error as error:
            self.query_one("#device-status", Static).update(
                _("Failed to load devices: {error}").format(error=error.message)
            )
            return
        for device in devices:
            device_id = device.get_id()
            name = device.get_name()
            if not device_id or not name:
                continue
            self.devices[device_id] = device
        self.rebuild_device_tree(self.query_one("#device-search", Input).value)
        self.update_remote_controls()

    def update_remote_controls(self) -> None:
        button = self.query_one("#enable-lvfs", Button)
        self.set_button_available(button, False)
        if self.client is None:
            return
        try:
            remotes = list(self.client.get_remotes() or [])
        except GLib.Error:
            return
        for remote in remotes:
            if remote.get_id() != "lvfs":
                continue
            self.set_button_available(
                button, not remote.has_flag(Fwupd.RemoteFlags.ENABLED)
            )
            return

    def rebuild_device_tree(self, search: str = "") -> None:
        tree = self.query_one("#device-tree", Tree)
        tree.clear()
        tree.show_root = False
        needle = search.casefold()

        def sort_key(device_id: str) -> str:
            return (self.devices[device_id].get_name() or "").casefold()

        parent_ids = {}
        for device_id, device in self.devices.items():
            parent_id = device.get_parent_id()
            if parent_id == device_id or parent_id not in self.devices:
                parent_id = None
            parent_ids[device_id] = parent_id

        matches = set()
        for device_id, device in self.devices.items():
            text = " ".join(
                filter(
                    None,
                    (
                        device.get_name(),
                        device.get_vendor(),
                        device.get_version(),
                        device.get_serial(),
                    ),
                )
            )
            if not needle or needle in text.casefold():
                matches.add(device_id)

        # Keep ancestors of search matches visible so the physical/logical
        # relationship remains clear, as it does in `fwupdmgr get-devices`.
        visible = set(matches)
        for device_id in matches:
            parent_id = parent_ids[device_id]
            seen = {device_id}
            while parent_id is not None and parent_id not in seen:
                visible.add(parent_id)
                seen.add(parent_id)
                parent_id = parent_ids[parent_id]

        children = {device_id: [] for device_id in visible}
        roots = []
        for device_id in visible:
            parent_id = parent_ids[device_id]
            if parent_id in visible:
                children[parent_id].append(device_id)
            else:
                roots.append(device_id)

        added = set()

        def add_device(parent_node, device_id: str) -> None:
            if device_id in added:
                return
            added.add(device_id)
            device = self.devices[device_id]
            label = Text(f"{device.get_name()}  {device.get_version() or ''}")
            child_ids = sorted(children[device_id], key=sort_key)
            if child_ids:
                node = parent_node.add(label, data=device_id, expand=True)
                for child_id in child_ids:
                    add_device(node, child_id)
            else:
                parent_node.add_leaf(label, data=device_id)

        for device_id in sorted(roots, key=sort_key):
            add_device(tree.root, device_id)
        # Malformed daemon data should not make a parent cycle disappear.
        for device_id in sorted(visible - added, key=sort_key):
            add_device(tree.root, device_id)
        tree.root.expand()
        self.query_one("#device-status", Static).update(
            _("{count} devices").format(count=len(matches))
            if self.devices
            else _("No supported devices were found")
        )

    @staticmethod
    def device_flag_names(device) -> List[str]:
        flags = (
            (Fwupd.DeviceFlags.INTERNAL, _("Internal device")),
            (Fwupd.DeviceFlags.UPDATABLE, _("Updatable")),
            (Fwupd.DeviceFlags.REQUIRE_AC, _("Requires AC power")),
            (Fwupd.DeviceFlags.LOCKED, _("Locked")),
            (Fwupd.DeviceFlags.SUPPORTED, _("Supported by a remote")),
            (Fwupd.DeviceFlags.NEEDS_REBOOT, _("Needs reboot")),
            (Fwupd.DeviceFlags.NEEDS_SHUTDOWN, _("Needs shutdown")),
            (Fwupd.DeviceFlags.NEEDS_ACTIVATION, _("Needs activation")),
            (Fwupd.DeviceFlags.DUAL_IMAGE, _("Stages updates")),
            (Fwupd.DeviceFlags.SELF_RECOVERY, _("Supports self-recovery")),
            (Fwupd.DeviceFlags.END_OF_LIFE, _("End of life")),
            (Fwupd.DeviceFlags.SIGNED_PAYLOAD, _("Signed payload")),
            (Fwupd.DeviceFlags.UNSIGNED_PAYLOAD, _("Unsigned payload")),
            (Fwupd.DeviceFlags.EMULATED, _("Emulated device")),
        )
        return [label for flag, label in flags if device.has_flag(flag)]

    def show_device(self, device_id: Optional[str]) -> None:
        self.current_device_id = device_id
        self.current_release_key = None
        install = self.query_one("#release-install", Button)
        install.label = _("Install")
        self.set_button_available(install, False)
        if device_id is None:
            self.query_one("#device-details", Static).update(
                _("Select a device to view its details.")
            )
            return
        device = self.devices[device_id]
        lines = [f"[b]{escape(device.get_name() or _('Unnamed device'))}[/b]"]
        fields = (
            (_("Version"), device.get_version()),
            (_("Minimum Version"), device.get_version_lowest()),
            (_("Bootloader Version"), device.get_version_bootloader()),
            (_("Vendor"), device.get_vendor()),
            (_("Branch"), device.get_branch()),
            (_("Serial Number"), device.get_serial()),
            (_("Install Duration"), format_duration(device.get_install_duration())),
            (
                _("Flashes Left"),
                str(device.get_flashes_left()) if device.get_flashes_left() else "",
            ),
            (_("Update Error"), device.get_update_error()),
        )
        for label, value in fields:
            if value:
                lines.append(f"[b]{escape(label)}:[/b] {escape(str(value))}")
        for label, values in (
            (_("Checksums"), list(device.get_checksums() or [])),
            (_("GUIDs"), list(device.get_guids() or [])),
            (_("Instance IDs"), list(device.get_instance_ids() or [])),
            (_("Security Issues"), list(device.get_issues() or [])),
            (_("Flags"), self.device_flag_names(device)),
        ):
            if values:
                lines.extend(("", f"[b]{escape(label)}[/b]"))
                lines.extend(f" • {escape(str(value))}" for value in values)
        self.query_one("#device-details", Static).update("\n".join(lines))
        self.set_button_available(
            self.query_one("#device-unlock", Button),
            device.has_flag(Fwupd.DeviceFlags.LOCKED),
        )
        self.set_button_available(
            self.query_one("#device-verify", Button),
            device.has_flag(Fwupd.DeviceFlags.CAN_VERIFY_IMAGE),
        )
        self.set_button_available(
            self.query_one("#device-verify-update", Button),
            device.has_flag(Fwupd.DeviceFlags.CAN_VERIFY),
        )
        self.set_button_available(
            self.query_one("#device-activate", Button),
            device.has_flag(Fwupd.DeviceFlags.NEEDS_ACTIVATION),
        )
        self.load_releases(device)

    def load_releases(self, device) -> None:
        tree = self.query_one("#release-tree", Tree)
        tree.clear()
        tree.show_root = False
        self.releases.clear()
        try:
            releases = list(self.client.get_releases(device.get_id()) or [])
        except GLib.Error:
            releases = []
        preferred_key = None
        for index, release in enumerate(releases):
            key = f"{device.get_id()}:{index}"
            self.releases[key] = release
            action = self.release_action(release)
            tree.root.add_leaf(
                Text(f"{release.get_version() or _('Unknown')}  [{action}]"), data=key
            )
            if preferred_key is None and release.has_flag(
                Fwupd.ReleaseFlags.IS_UPGRADE
            ):
                preferred_key = key
        tree.root.expand()
        tree.root.label = Text(
            _("Available Releases ({count})").format(count=len(releases))
        )
        if preferred_key is not None:
            self.set_release_action(preferred_key)

    @staticmethod
    def release_action(release) -> str:
        if release.has_flag(Fwupd.ReleaseFlags.IS_UPGRADE):
            return _("Update")
        if release.has_flag(Fwupd.ReleaseFlags.IS_DOWNGRADE):
            return _("Downgrade")
        return _("Reinstall")

    def show_release(self, key: Optional[str]) -> None:
        if key is None or self.current_device_id is None:
            self.current_release_key = None
            button = self.query_one("#release-install", Button)
            button.label = _("Install")
            self.set_button_available(button, False)
            return
        device = self.devices[self.current_device_id]
        release = self.releases[key]
        action = self.set_release_action(key)
        lines = [
            f"[b]{escape(release.get_name() or device.get_name() or _('Firmware'))} "
            f"{escape(release.get_version() or '')}[/b]"
        ]
        fields = (
            (_("Action"), action),
            (_("Summary"), release.get_summary()),
            (_("Vendor"), release.get_vendor()),
            (_("Remote"), release.get_remote_id()),
            (_("Branch"), release.get_branch()),
            (_("Size"), format_size(release.get_size()) if release.get_size() else ""),
            (_("Install Duration"), format_duration(release.get_install_duration())),
            (_("Update Message"), release.get_update_message()),
        )
        for label, value in fields:
            if value:
                lines.append(f"[b]{escape(label)}:[/b] {escape(str(value))}")
        description = xml_to_text(release.get_description())
        if description:
            lines.extend(("", escape(description)))
        for label, values in (
            (_("Issues"), list(release.get_issues() or [])),
            (_("Checksums"), list(release.get_checksums() or [])),
        ):
            if values:
                lines.extend(("", f"[b]{escape(label)}[/b]"))
                lines.extend(f" • {escape(str(value))}" for value in values)
        self.query_one("#device-details", Static).update("\n".join(lines))

    def set_release_action(self, key: str) -> str:
        self.current_release_key = key
        device = self.devices[self.current_device_id]
        release = self.releases[key]
        action = self.release_action(release)
        button = self.query_one("#release-install", Button)
        button.label = action
        self.set_button_available(button, device.has_flag(Fwupd.DeviceFlags.UPDATABLE))
        return action

    @staticmethod
    def install_flags_for_release(release):
        flags = Fwupd.InstallFlags.NONE
        if release.has_flag(Fwupd.ReleaseFlags.IS_DOWNGRADE):
            flags |= Fwupd.InstallFlags.ALLOW_OLDER
        elif not release.has_flag(Fwupd.ReleaseFlags.IS_UPGRADE):
            flags |= Fwupd.InstallFlags.ALLOW_REINSTALL
        if release.has_flag(Fwupd.ReleaseFlags.IS_ALTERNATE_BRANCH):
            flags |= Fwupd.InstallFlags.ALLOW_BRANCH_SWITCH
        return flags

    def confirm_release_install(self) -> None:
        if self.current_device_id is None or self.current_release_key is None:
            return
        device = self.devices[self.current_device_id]
        release = self.releases[self.current_release_key]
        action = self.release_action(release)
        warnings = []
        if device.has_flag(Fwupd.DeviceFlags.AFFECTS_FDE):
            warnings.append(
                _("Suspend full-disk encryption protection before continuing.")
            )
        if release.has_flag(Fwupd.ReleaseFlags.IS_ALTERNATE_BRANCH):
            warnings.append(_("This release switches to an alternate firmware branch."))
        if release.has_flag(Fwupd.ReleaseFlags.IS_COMMUNITY):
            warnings.append(_("This firmware was built by the community."))
        message = _("{action} {device} to version {version}?").format(
            action=action,
            device=device.get_name(),
            version=release.get_version(),
        )
        if warnings:
            message += "\n\n" + "\n".join(warnings)

        def complete(confirmed: bool) -> None:
            if not confirmed:
                return
            self.run_device_operation(
                _("Installing firmware"),
                lambda: self.client.install_release(
                    device,
                    release,
                    self.install_flags_for_release(release),
                    Fwupd.ClientDownloadFlags.NONE,
                ),
                _("Firmware installation completed"),
            )

        self.push_screen(ConfirmAction(action, message, action), complete)

    def device_error(self, prefix: str, error: GLib.Error) -> None:
        self.query_one("#device-status", Static).update(
            _("{prefix}: {error}").format(prefix=prefix, error=error.message)
        )

    def run_device_operation(
        self,
        title: str,
        operation: Callable[[], object],
        success: str,
        refresh_devices: bool = True,
    ) -> None:
        status = self.query_one("#device-status", Static)
        status.update(_("{operation}…").format(operation=title))

        def complete(message: str) -> None:
            status.update(message)
            if refresh_devices:
                self.load_devices()

        def run() -> None:
            try:
                operation()
            except GLib.Error as error:
                message = _("{prefix}: {error}").format(
                    prefix=title, error=error.message
                )
                self.call_from_thread(status.update, message)
                return
            self.call_from_thread(complete, success)

        self.run_worker(
            run,
            name=title,
            group="fwupd-device-operation",
            exclusive=True,
            thread=True,
        )

    def install_local_file(self, path: Optional[str]) -> None:
        if path is None or self.client is None:
            return
        try:
            details = list(self.client.get_details(path) or [])
        except GLib.Error as error:
            self.device_error(_("Failed to inspect firmware file"), error)
            return
        if not details:
            self.query_one("#device-status", Static).update(
                _("The firmware archive contains no installable releases")
            )
            return
        archive_device = details[0]
        release = archive_device.get_release_default()
        version = release.get_version() if release is not None else _("unknown")
        name = archive_device.get_name() or _("firmware")
        flags = (
            self.install_flags_for_release(release)
            if release is not None
            else Fwupd.InstallFlags.NONE
        )
        message = _("Install {name} version {version} from {path}?").format(
            name=name, version=version, path=path
        )

        def complete(confirmed: bool) -> None:
            if not confirmed:
                return
            self.run_device_operation(
                _("Installing firmware"),
                lambda: self.client.install(Fwupd.DEVICE_ID_ANY, path, flags),
                _("Local firmware installation completed"),
            )

        self.push_screen(
            ConfirmAction(_("Install Firmware"), message, _("Install")), complete
        )

    def refresh_metadata(self) -> None:
        if self.client is None:
            return

        def operation() -> None:
            remotes = list(self.client.get_remotes() or [])
            for remote in remotes:
                if not remote.has_flag(Fwupd.RemoteFlags.ENABLED):
                    continue
                if remote.get_kind() != Fwupd.RemoteKind.DOWNLOAD:
                    continue
                self.client.refresh_remote(remote, Fwupd.ClientDownloadFlags.NONE)

        self.run_device_operation(
            _("Refreshing metadata"),
            operation,
            _("Firmware metadata refreshed"),
            refresh_devices=False,
        )

    def enable_lvfs(self) -> None:
        if self.client is None:
            return

        def operation() -> None:
            self.client.modify_remote("lvfs", "Enabled", "true")
            remote = next(
                (
                    item
                    for item in list(self.client.get_remotes() or [])
                    if item.get_id() == "lvfs"
                ),
                None,
            )
            if remote is not None:
                self.client.refresh_remote(remote, Fwupd.ClientDownloadFlags.NONE)

        self.run_device_operation(
            _("Enabling LVFS"),
            operation,
            _("LVFS was enabled and its metadata refreshed"),
        )

    # Security --------------------------------------------------------------

    @staticmethod
    def security_result(attr) -> str:
        return SECURITY_RESULTS.get(
            attr.get_result(),
            _("OK") if attr.has_flag(Fwupd.SecurityAttrFlags.SUCCESS) else _("Unknown"),
        )

    def load_security(self) -> None:
        tree = self.query_one("#security-tree", Tree)
        tree.clear()
        tree.show_root = False
        self.security_attrs.clear()
        if self.client is None:
            self.query_one("#security-status", Static).update(
                _("fwupd is not connected")
            )
            return
        try:
            attrs = list(self.client.get_host_security_attrs() or [])
        except GLib.Error as error:
            self.query_one("#security-status", Static).update(
                _("Failed to load security attributes: {error}").format(
                    error=error.message
                )
            )
            return
        groups = {}
        for level, label in SECURITY_LEVELS.items():
            groups[level] = tree.root.add(Text(label), expand=True)
        runtime = tree.root.add(Text(_("Runtime Suffix")), expand=True)
        for index, attr in enumerate(attrs):
            if attr.has_flag(Fwupd.SecurityAttrFlags.OBSOLETED):
                continue
            key = f"security:{index}"
            self.security_attrs[key] = attr
            name = attr.get_name() or attr.get_appstream_id() or _("Unknown attribute")
            marker = "✔" if attr.has_flag(Fwupd.SecurityAttrFlags.SUCCESS) else "✘"
            label = Text(f"{marker} {_(name)}: {self.security_result(attr)}")
            parent = groups.get(attr.get_level(), tree.root)
            parent.add_leaf(label, data=key)
            if attr.has_flag(Fwupd.SecurityAttrFlags.RUNTIME_ISSUE):
                runtime.add_leaf(label.copy(), data=key)
        tree.root.expand()
        host_id = self.client.get_host_security_id() or _("Unavailable")
        self.query_one("#security-summary", Static).update(
            f"[b]{escape(_('Host Security ID'))}:[/b] {escape(host_id)}"
        )
        self.query_one("#security-status", Static).update(
            _("{count} security attributes").format(count=len(self.security_attrs))
        )
        self.load_security_events()

    def load_security_events(self) -> None:
        lines = [f"[b]{escape(_('Recent Security Events'))}[/b]"]
        try:
            events = list(self.client.get_host_security_events(10) or [])
        except GLib.Error:
            events = []
        for event in events:
            old = SECURITY_RESULTS.get(event.get_result_fallback(), _("Unknown"))
            new = SECURITY_RESULTS.get(event.get_result(), _("Unknown"))
            name = (
                event.get_name() or event.get_appstream_id() or _("Security attribute")
            )
            created = event.get_created()
            timestamp = (
                datetime.datetime.fromtimestamp(
                    created, tz=datetime.timezone.utc
                ).strftime("%Y-%m-%d %H:%M")
                if created
                else ""
            )
            lines.append(
                _("{time} {name}: {old} → {new}").format(
                    time=timestamp, name=_(name), old=old, new=new
                )
            )
        issues = []
        try:
            devices = list(self.client.get_devices() or [])
        except GLib.Error:
            devices = []
        for device in devices:
            for issue in list(device.get_issues() or []):
                issues.append(
                    _("{device}: {issue}").format(
                        device=device.get_name() or _("Unknown device"), issue=issue
                    )
                )
        if issues:
            lines.extend(("", f"[b]{escape(_('Device Security Issues'))}[/b]"))
            lines.extend(f" • {escape(issue)}" for issue in issues)
        if len(lines) == 1:
            lines.append(_("No recent security events"))
        self.query_one("#security-events", Static).update("\n".join(lines))

    def show_security_attr(self, key: Optional[str]) -> None:
        details = self.query_one("#security-details", Static)
        if key is None:
            details.update(_("Select a security attribute to view its details."))
            return
        attr = self.security_attrs[key]
        name = attr.get_name() or attr.get_appstream_id() or _("Security attribute")
        lines = [
            f"[b]{escape(_(name))}[/b]",
            f"[b]{escape(_('Result'))}:[/b] {escape(self.security_result(attr))}",
        ]
        level = SECURITY_LEVELS.get(attr.get_level())
        fields = (
            (_("Level"), level),
            (_("Plugin"), attr.get_plugin()),
            (_("Firmware Setting"), attr.get_bios_setting_id()),
            (_("Current Firmware Value"), attr.get_bios_setting_current_value()),
            (_("Target Firmware Value"), attr.get_bios_setting_target_value()),
            (_("Current Kernel Value"), attr.get_kernel_current_value()),
            (_("Target Kernel Value"), attr.get_kernel_target_value()),
            (_("More Information"), attr.get_url()),
        )
        for label, value in fields:
            if value:
                lines.append(f"[b]{escape(label)}:[/b] {escape(str(value))}")
        description = attr.get_description()
        if description:
            lines.extend(("", escape(_(description))))
        actions = []
        if attr.has_flag(Fwupd.SecurityAttrFlags.ACTION_CONFIG_FW):
            actions.append(_("Change the firmware configuration"))
        if attr.has_flag(Fwupd.SecurityAttrFlags.ACTION_CONFIG_OS):
            actions.append(_("Change the operating system configuration"))
        if attr.has_flag(Fwupd.SecurityAttrFlags.ACTION_CONTACT_OEM):
            actions.append(_("Contact the hardware vendor"))
        if actions:
            lines.extend(("", f"[b]{escape(_('Suggested Actions'))}[/b]"))
            lines.extend(f" • {escape(action)}" for action in actions)
        details.update("\n".join(lines))

    # Events ----------------------------------------------------------------

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "search":
            self.show_details(None)
            self.rebuild_tree(event.value)
        elif event.input.id == "device-search":
            self.rebuild_device_tree(event.value)

    def on_tree_node_highlighted(self, event: Tree.NodeHighlighted) -> None:
        tree_id = event.control.id
        key = event.node.data if isinstance(event.node.data, str) else None
        if tree_id == "settings-tree":
            self.show_details(key)
        elif tree_id == "device-tree":
            self.show_device(key)
        elif tree_id == "release-tree":
            self.show_release(key)
        elif tree_id == "security-tree":
            self.show_security_attr(key)

    def on_tree_node_selected(self, event: Tree.NodeSelected) -> None:
        tree_id = event.control.id
        if not isinstance(event.node.data, str):
            return
        if tree_id == "settings-tree":
            self.show_details(event.node.data)
            self.action_edit()
        elif tree_id == "device-tree":
            self.show_device(event.node.data)
        elif tree_id == "release-tree":
            self.show_release(event.node.data)
        elif tree_id == "security-tree":
            self.show_security_attr(event.node.data)

    def on_button_pressed(self, event: Button.Pressed) -> None:
        button_id = event.button.id
        if button_id == "home-bios":
            self.show_view("bios-view")
        elif button_id == "home-devices":
            self.show_view("devices-view")
        elif button_id == "home-security":
            self.show_view("security-view")
        elif button_id == "home-quit":
            self.exit()
        elif button_id in ("bios-home", "devices-home", "security-home"):
            self.action_home()
        elif button_id == "edit":
            self.action_edit()
        elif button_id == "apply":
            self.action_apply()
        elif button_id == "devices-refresh":
            self.load_devices()
        elif button_id == "metadata-refresh":
            self.refresh_metadata()
        elif button_id == "enable-lvfs":
            self.enable_lvfs()
        elif button_id == "install-file":
            self.push_screen(PathPrompt(), self.install_local_file)
        elif button_id == "release-install":
            self.confirm_release_install()
        elif button_id == "device-unlock" and self.current_device_id:
            self.run_device_operation(
                _("Unlocking device"),
                lambda: self.client.unlock(self.current_device_id),
                _("Device unlocked"),
            )
        elif button_id == "device-verify" and self.current_device_id:
            self.run_device_operation(
                _("Verifying firmware"),
                lambda: self.client.verify(self.current_device_id),
                _("Firmware verification succeeded"),
            )
        elif button_id == "device-verify-update" and self.current_device_id:
            self.run_device_operation(
                _("Updating verification data"),
                lambda: self.client.verify_update(self.current_device_id),
                _("Verification data updated"),
            )
        elif button_id == "device-activate" and self.current_device_id:
            self.run_device_operation(
                _("Activating firmware"),
                lambda: self.client.activate(device_id=self.current_device_id),
                _("Device firmware activated"),
            )
        elif button_id == "security-refresh":
            self.load_security()


def load_settings(client) -> List[BiosSetting]:
    """Fetch settings through libfwupd's introspected client API."""
    return [BiosSetting.from_fwupd(setting) for setting in client.get_bios_settings()]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=_("Manage firmware and platform security in a terminal interface")
    )
    parser.add_argument("--version", action="version", version=PACKAGE_VERSION)
    return parser.parse_args()


def main() -> int:
    parse_args()
    try:
        client = Fwupd.Client()
    except (GLib.Error, TypeError) as error:
        print(
            _("Failed to initialize fwupd: {error}").format(error=error),
            file=sys.stderr,
        )
        return 1
    FwupdTui(client=client).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
