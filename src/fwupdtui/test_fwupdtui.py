#!/usr/bin/env python3
#
# Copyright 2026 Advanced Micro Devices, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Tests for fwupdtui."""

import asyncio

import pytest

from textual.widgets import Button, ContentSwitcher, Input, Select, Static, Tree

from fwupdtui import BiosSetting, Fwupd, FwupdTui, SettingEditor


def make_setting(**kwargs) -> BiosSetting:
    values = {
        "key": "com.example.setting",
        "name": "Network Boot",
        "parent": "Boot/Network",
        "description": "Choose the network boot protocol",
        "current_value": "HTTP",
        "kind": Fwupd.BiosSettingKind.ENUMERATION,
        "read_only": False,
        "possible_values": ["HTTP", "PXE"],
        "lower_bound": 0,
        "upper_bound": 0,
        "scalar_increment": 1,
    }
    values.update(kwargs)
    return BiosSetting(**values)


class FakeFwupdObject:
    """Small Python stand-in for objects returned by libfwupd introspection."""

    def __init__(self, *, flags=(), **values):
        self.flags = set(flags)
        self.values = values

    def __getattr__(self, name):
        if name.startswith("get_"):
            return lambda: self.values.get(name.removeprefix("get_"))
        raise AttributeError(name)

    def has_flag(self, flag) -> bool:
        return flag in self.flags


class TestBiosSetting:
    def test_from_fwupd(self) -> None:
        raw = FakeFwupdObject(
            name="NetworkBoot",
            id="com.example.network-boot",
            parent="Boot/Network",
            description="Choose the network boot protocol",
            kind=Fwupd.BiosSettingKind.ENUMERATION,
            current_value="HTTP",
            possible_values=["HTTP", "PXE"],
            read_only=False,
            lower_bound=0,
            upper_bound=0,
            scalar_increment=1,
        )

        setting = BiosSetting.from_fwupd(raw)
        assert setting.key == "com.example.network-boot"
        assert setting.parent == "Boot/Network"
        assert setting.possible_values == ["HTTP", "PXE"]

    def test_from_fwupd_integer(self) -> None:
        raw = FakeFwupdObject(
            name="ThreadCount",
            id="com.example.thread-count",
            kind=Fwupd.BiosSettingKind.INTEGER,
            current_value="16",
            read_only=False,
            lower_bound=1,
            upper_bound=128,
            scalar_increment=1,
        )

        setting = BiosSetting.from_fwupd(raw)
        assert setting.possible_values == []
        assert setting.lower_bound == 1
        assert setting.upper_bound == 128

    def test_from_fwupd_without_parent_accessor(self) -> None:
        class LegacySetting(FakeFwupdObject):
            def __getattr__(self, name):
                if name == "get_parent":
                    raise AttributeError(name)
                return super().__getattr__(name)

        raw = LegacySetting(
            name="NetworkBoot",
            kind=Fwupd.BiosSettingKind.ENUMERATION,
            current_value="HTTP",
            possible_values=[],
            read_only=False,
            lower_bound=0,
            upper_bound=0,
            scalar_increment=1,
        )

        setting = BiosSetting.from_fwupd(raw)
        assert setting.parent == ""

    def test_enumeration(self) -> None:
        setting = make_setting()
        assert setting.validate("PXE") == "PXE"
        with pytest.raises(ValueError, match="available values"):
            setting.validate("invalid")

    def test_integer(self) -> None:
        setting = make_setting(
            kind=Fwupd.BiosSettingKind.INTEGER,
            current_value="0x10",
            possible_values=[],
            lower_bound=0,
            upper_bound=32,
            scalar_increment=4,
        )
        assert setting.validate("0x10") == "16"
        with pytest.raises(ValueError, match="increment"):
            setting.validate("17")
        with pytest.raises(ValueError, match="between"):
            setting.validate("36")

    def test_string(self) -> None:
        setting = make_setting(
            kind=Fwupd.BiosSettingKind.STRING,
            current_value="fwupd",
            possible_values=[],
            lower_bound=3,
            upper_bound=8,
        )
        assert setting.validate("firmware") == "firmware"
        with pytest.raises(ValueError, match="length"):
            setting.validate("no")


class TestFwupdTui:
    def test_escape_from_bios_selector(self) -> None:
        setting = make_setting()
        app = FwupdTui([setting])

        async def run_test() -> None:
            async with app.run_test(size=(100, 30)) as pilot:
                app.show_view("bios-view")
                app.current_key = setting.key
                app.action_edit()
                await pilot.pause()
                assert isinstance(app.screen, SettingEditor)
                await pilot.press("enter")
                await pilot.press("escape")
                assert isinstance(app.screen, SettingEditor)

        asyncio.run(run_test())

    def test_bios_selector_without_current_value(self) -> None:
        setting = make_setting(current_value="", possible_values=["", "PXE"])
        app = FwupdTui([setting])

        async def run_test() -> None:
            async with app.run_test(size=(100, 30)) as pilot:
                app.show_view("bios-view")
                app.current_key = setting.key
                app.action_edit()
                await pilot.pause()
                editor = app.screen.query_one("#editor-value", Select)
                assert editor.value not in setting.possible_values
                assert not editor.disabled
                app.pop_screen()
                await pilot.pause()

        asyncio.run(run_test())

    def test_home_keyboard_navigation(self) -> None:
        app = FwupdTui([])

        async def run_test() -> None:
            async with app.run_test(size=(100, 30)) as pilot:
                assert app.focused.id == "home-bios"
                assert app.query_one("#home-bios", Button).variant == "primary"
                await pilot.press("down")
                assert app.focused.id == "home-devices"
                assert app.query_one("#home-bios", Button).variant == "default"
                assert app.query_one("#home-devices", Button).variant == "primary"
                await pilot.press("enter")
                assert app.current_view == "devices-view"
                app.action_home()
                await pilot.press("3")
                assert app.current_view == "security-view"

        asyncio.run(run_test())

    def test_grouping_and_pending_change(self) -> None:
        setting = make_setting()
        app = FwupdTui([setting])

        async def run_test() -> None:
            async with app.run_test(size=(100, 30)) as pilot:
                await pilot.pause()
                tree = app.query_one("#settings-tree", Tree)
                assert tree.root.children[0].label.plain == "Boot"
                assert tree.root.children[0].children[0].label.plain == "Network"
                app.current_key = setting.key
                app.edit_complete("PXE")
                assert app.pending == {setting.key: "PXE"}
                apply = app.query_one("#apply", Button)
                assert not apply.disabled
                assert apply.display

        asyncio.run(run_test())

    def test_home_and_device_view(self) -> None:
        parent_id = "1" * 40
        device_id = "0" * 40
        parent = FakeFwupdObject(
            id=parent_id,
            name="System Firmware",
            version="0.5",
        )
        device = FakeFwupdObject(
            id=device_id,
            parent_id=parent_id,
            name="Test Device",
            vendor="ACME",
            version="1.0",
            install_duration=0,
            flashes_left=0,
            flags=[Fwupd.DeviceFlags.UPDATABLE],
        )
        release = FakeFwupdObject(
            name="Test Firmware",
            version="2.0",
            summary="A firmware update",
            size=0,
            install_duration=0,
            flags=[Fwupd.ReleaseFlags.IS_UPGRADE],
        )
        downgrade = FakeFwupdObject(
            version="0.9",
            size=0,
            install_duration=0,
            flags=[Fwupd.ReleaseFlags.IS_DOWNGRADE],
        )

        class Client:
            def get_devices(self):
                return [device, parent]

            def get_releases(self, device_id):
                assert device_id == "0" * 40
                return [release, downgrade]

            def get_remotes(self):
                return []

        app = FwupdTui(client=Client())

        async def run_test() -> None:
            async with app.run_test(size=(80, 24)) as pilot:
                assert app.has_class("compact")
                assert app.has_class("narrow")
                assert app.has_class("short")
                assert app.query_one("#views", ContentSwitcher).current == "home-view"
                await pilot.click("#home-devices")
                assert (
                    app.query_one("#views", ContentSwitcher).current == "devices-view"
                )
                tree = app.query_one("#device-tree", Tree)
                assert tree.root.children[0].label.plain == "System Firmware  0.5"
                assert (
                    tree.root.children[0].children[0].label.plain == "Test Device  1.0"
                )
                app.query_one("#device-search", Input).value = "Test Device"
                await pilot.pause()
                assert tree.root.children[0].label.plain == "System Firmware  0.5"
                assert len(tree.root.children[0].children) == 1
                app.show_device(device_id)
                assert "ACME" in app.query_one("#device-details", Static).render().plain
                update = app.query_one("#release-install", Button)
                assert update.label.plain == "Update"
                assert not update.disabled
                assert update.display
                assert not app.query_one("#device-unlock", Button).display
                assert not app.query_one("#device-activate", Button).display
                assert (
                    app.query_one("#device-verify-update", Button).label.plain
                    == "Store"
                )
                assert update.region.bottom <= app.screen.size.height
                releases = app.query_one("#release-tree", Tree)
                assert releases.root.children[0].label.plain == "2.0  [Update]"
                app.show_release(f"{device_id}:1")
                assert update.label.plain == "Downgrade"

        asyncio.run(run_test())

    def test_security_view(self) -> None:
        attr = FakeFwupdObject(
            appstream_id="org.fwupd.hsi.Test",
            name="Test Protection",
            description="Protects the test device",
            level=Fwupd.SecurityAttrLevel.CRITICAL,
            result=Fwupd.SecurityAttrResult.ENABLED,
            flags=[Fwupd.SecurityAttrFlags.SUCCESS],
        )

        class Client:
            def get_host_security_attrs(self):
                return [attr]

            def get_host_security_events(self, limit):
                assert limit == 10
                return []

            def get_host_security_id(self):
                return "HSI:1"

            def get_devices(self):
                return []

        app = FwupdTui(client=Client())

        async def run_test() -> None:
            async with app.run_test(size=(120, 40)) as pilot:
                await pilot.click("#home-security")
                tree = app.query_one("#security-tree", Tree)
                assert tree.root.children[0].label.plain == "HSI-1 Critical"
                assert (
                    tree.root.children[0]
                    .children[0]
                    .label.plain.endswith("Test Protection: Enabled")
                )
                assert (
                    "HSI:1" in app.query_one("#security-summary", Static).render().plain
                )

        asyncio.run(run_test())
