#!/usr/bin/env python3
#
# Copyright 2026 Advanced Micro Devices, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""A Textual interface for viewing and changing firmware settings."""

import argparse
import gettext
import os
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional

import gi
from rich.markup import escape
from rich.text import Text
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import Button, Footer, Header, Input, Label, Select, Static, Tree

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
            parent=setting.get_parent() or "",
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
                yield Select(
                    options, value=self.value, allow_blank=False, id="editor-value"
                )
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
            value = "" if value is Select.BLANK else str(value)
        else:
            value = self.query_one("#editor-input", Input).value
        try:
            self.dismiss(self.setting.validate(value))
        except ValueError as error:
            self.query_one("#editor-error", Static).update(str(error))


class FwupdTui(App):
    """Interactive BIOS setting browser."""

    TITLE = _("fwupd BIOS Settings")
    SUB_TITLE = _("View and modify firmware configuration")

    CSS = """
    #body {
        height: 1fr;
    }
    #browser {
        width: 2fr;
        min-width: 32;
        border-right: solid $primary;
    }
    #search {
        margin: 1;
    }
    #settings-tree {
        height: 1fr;
        padding: 0 1;
    }
    #details-pane {
        width: 3fr;
        min-width: 38;
        padding: 1 2;
    }
    #details {
        height: 1fr;
    }
    #status {
        height: auto;
        color: $text-muted;
        margin-bottom: 1;
    }
    #buttons {
        height: auto;
        align-horizontal: right;
    }
    #buttons Button {
        margin-left: 1;
    }
    """

    BINDINGS = [
        ("q", "quit_without_changes", _("Quit")),
        ("slash", "focus_search", _("Search")),
        ("e", "edit", _("Edit")),
        ("ctrl+s", "apply", _("Apply")),
    ]

    def __init__(self, settings: List[BiosSetting]) -> None:
        super().__init__()
        self.settings = sorted(settings, key=lambda item: (item.parent, item.name))
        self.settings_by_key = {setting.key: setting for setting in self.settings}
        self.pending: Dict[str, str] = {}
        self.current_key: Optional[str] = None

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal(id="body"):
            with Vertical(id="browser"):
                yield Input(placeholder=_("Search BIOS settings"), id="search")
                yield Tree(_("BIOS Settings"), id="settings-tree")
            with Vertical(id="details-pane"):
                yield Static(_("Select a setting to view its details."), id="details")
                yield Static("", id="status")
                with Horizontal(id="buttons"):
                    yield Button(_("Edit"), id="edit", disabled=True)
                    yield Button(_("Quit"), id="quit")
                    yield Button(
                        _("Apply"), id="apply", variant="success", disabled=True
                    )
        yield Footer()

    def on_mount(self) -> None:
        self.rebuild_tree()
        self.query_one("#settings-tree", Tree).focus()

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
        self.query_one("#status", Static).update(
            _("{count} settings").format(count=visible)
        )

    def show_details(self, key: Optional[str]) -> None:
        self.current_key = key
        details = self.query_one("#details", Static)
        edit = self.query_one("#edit", Button)
        if key is None:
            details.update(_("Select a setting to view its details."))
            edit.disabled = True
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
        edit.disabled = setting.read_only

    def edit_complete(self, value: Optional[str]) -> None:
        if value is None or self.current_key is None:
            return
        setting = self.settings_by_key[self.current_key]
        if value == setting.current_value:
            self.pending.pop(setting.key, None)
        else:
            self.pending[setting.key] = value
        self.query_one("#apply", Button).disabled = not self.pending
        search = self.query_one("#search", Input).value
        self.rebuild_tree(search)
        self.show_details(setting.key)

    def action_edit(self) -> None:
        if self.current_key is None:
            return
        setting = self.settings_by_key[self.current_key]
        if setting.read_only:
            return
        value = self.pending.get(setting.key, setting.current_value)
        self.push_screen(SettingEditor(setting, value), self.edit_complete)

    def action_apply(self) -> None:
        if self.pending:
            self.exit(dict(self.pending))

    def action_quit_without_changes(self) -> None:
        self.exit({})

    def action_focus_search(self) -> None:
        self.query_one("#search", Input).focus()

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "search":
            self.show_details(None)
            self.rebuild_tree(event.value)

    def on_tree_node_highlighted(self, event: Tree.NodeHighlighted) -> None:
        key = event.node.data if isinstance(event.node.data, str) else None
        self.show_details(key)

    def on_tree_node_selected(self, event: Tree.NodeSelected) -> None:
        if isinstance(event.node.data, str):
            self.show_details(event.node.data)
            self.action_edit()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "edit":
            self.action_edit()
        elif event.button.id == "apply":
            self.action_apply()
        elif event.button.id == "quit":
            self.action_quit_without_changes()


def load_settings(client) -> List[BiosSetting]:
    """Fetch settings through libfwupd's introspected client API."""
    return [BiosSetting.from_fwupd(setting) for setting in client.get_bios_settings()]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=_("View and modify BIOS settings in a terminal interface")
    )
    parser.add_argument("--version", action="version", version=PACKAGE_VERSION)
    return parser.parse_args()


def main() -> int:
    parse_args()
    try:
        client = Fwupd.Client()
        settings = load_settings(client)
    except GLib.Error as error:
        print(
            _("Failed to load BIOS settings: {error}").format(error=error.message),
            file=sys.stderr,
        )
        return 1
    if not settings:
        print(_("This system doesn't support firmware settings"), file=sys.stderr)
        return 1
    changes = FwupdTui(settings).run()
    if not changes:
        return 0
    try:
        client.modify_bios_setting(changes)
    except GLib.Error as error:
        print(
            _("Failed to modify BIOS settings: {error}").format(error=error.message),
            file=sys.stderr,
        )
        return 1
    print(_("BIOS settings were changed successfully. A reboot may be required."))
    return 0


if __name__ == "__main__":
    sys.exit(main())
